#include "gtkprintbackendcpdb.h"
#include "config.h"
#include "gtkcpdbutils.h"
#include "gtkintl.h"
#include "gtkprintercpdb.h"
#include "gtkprinterprivate.h"
#include <cpdb-libs-frontend.h>
#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#ifdef HAVE_COLORD
#include <colord.h>
#endif

#define AVAHI_IF_UNSPEC -1
#define AVAHI_PROTO_INET 0
#define AVAHI_PROTO_INET6 1
#define AVAHI_PROTO_UNSPEC -1

#define AVAHI_BUS "org.freedesktop.Avahi"
#define AVAHI_SERVER_IFACE "org.freedesktop.Avahi.Server"
#define AVAHI_SERVICE_BROWSER_IFACE "org.freedesktop.Avahi.ServiceBrowser"
#define AVAHI_SERVICE_RESOLVER_IFACE "org.freedesktop.Avahi.ServiceResolver"

#define UNSIGNED_FLOAT_REGEX "([0-9]+([.,][0-9]*)?|[.,][0-9]+)([e][+-]?[0-9]+)?"
#define SIGNED_FLOAT_REGEX "[+-]?" UNSIGNED_FLOAT_REGEX
#define SIGNED_INTEGER_REGEX "[+-]?([0-9]+)"

#define PRINTER_NAME_ALLOWED_CHARACTERS "abcdefghijklmnopqrtsuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_"
/* define this to see warnings about ignored ppd options */
#undef PRINT_IGNORED_OPTIONS

#define _CPDB_MAP_ATTR_INT(attr, v, a)         \
  {                                            \
    if (!g_ascii_strcasecmp (attr->name, (a))) \
      v = attr->values[0].integer;             \
  }
#define _CPDB_MAP_ATTR_STR(attr, v, a)         \
  {                                            \
    if (!g_ascii_strcasecmp (attr->name, (a))) \
      v = attr->values[0].string.text;         \
  }

G_DEFINE_DYNAMIC_TYPE (GtkPrintBackendCpdb, gtk_print_backend_cpdb, GTK_TYPE_PRINT_BACKEND)

void
g_io_module_load (GIOModule *module)
{
  g_type_module_use (G_TYPE_MODULE (module));

  gtk_print_backend_cpdb_register_type (G_TYPE_MODULE (module));

  g_io_extension_point_implement (GTK_PRINT_BACKEND_EXTENSION_POINT_NAME,
                                  GTK_TYPE_PRINT_BACKEND_CPDB,
                                  "cpdb",
                                  10);
}

void
g_io_module_unload (GIOModule *module)
{
}

char **
g_io_module_query (void)
{
  char *eps[] = {
    (char *) GTK_PRINT_BACKEND_EXTENSION_POINT_NAME,
    NULL
  };

  return g_strdupv (eps);
}

static GType gtk_printer_cpdb_type = 0;

enum
{
  PROP_0,
  PROP_PROFILE_TITLE
};

typedef struct
{
  GSource source;

  http_t *http;
  GtkCpdbRequest *request;
  GtkCpdbPollState poll_state;
  GPollFD *data_poll;
  GtkPrintBackendCpdb *backend;
  GtkPrintCpdbResponseCallbackFunc callback;
  gpointer callback_data;

} GtkPrintCpdbDispatchWatch;

typedef struct
{
  GtkPrintJobCompleteFunc callback;
  GtkPrintJob *job;
  gpointer user_data;
  GDestroyNotify dnotify;
  http_t *http;
} CpdbPrintStreamData;

GtkPrintBackend *
gtk_print_backend_cpdb_new (void)
{
  return g_object_new (GTK_TYPE_PRINT_BACKEND_CPDB, NULL);
}

static void
gtk_print_backend_cpdb_class_init (GtkPrintBackendCpdbClass *class)
{
  GtkPrintBackendClass *backend_class = GTK_PRINT_BACKEND_CLASS (class);

  backend_parent_class = g_type_class_peek_parent (class);

  backend_class->request_printer_list = cpdb_get_printer_list;
  backend_class->printer_create_cairo_surface = cpdb_printer_create_cairo_surface;
  backend_class->printer_get_options = cpdb_printer_get_options;
  backend_class->printer_get_settings_from_options = cpdb_printer_get_settings_from_options;
  backend_class->printer_prepare_for_print = cpdb_printer_prepare_for_print;
}

static GtkPrinterClass *gtk_printer_cpdb_parent_class;

/* This function frees memory used by the GtkCpdbConnectionTest structure.
 */

static gboolean
cpdb_dispatch_watch_dispatch (GSource *source,
                              GSourceFunc callback,
                              gpointer user_data)
{
  GtkPrintCpdbDispatchWatch *dispatch;
  GtkPrintCpdbResponseCallbackFunc ep_callback;
  GtkCpdbResult *result;

  g_assert (callback != NULL);

  ep_callback = (GtkPrintCpdbResponseCallbackFunc) callback;

  dispatch = (GtkPrintCpdbDispatchWatch *) source;

  result = gtk_cpdb_request_get_result (dispatch->request);

  GTK_NOTE (PRINTING,
            g_print ("CPDB: %s <source %p>\n", G_STRFUNC, source));

  if (gtk_cpdb_result_is_error (result))
    {
      GTK_NOTE (PRINTING,
                g_print ("Error result: %s (type %i, status %i, code %i)\n",
                         gtk_cpdb_result_get_error_string (result),
                         gtk_cpdb_result_get_error_type (result),
                         gtk_cpdb_result_get_error_status (result),
                         gtk_cpdb_result_get_error_code (result)));
    }

  ep_callback (GTK_PRINT_BACKEND (dispatch->backend), result, user_data);

  return FALSE;
}

static gboolean
request_password (gpointer data)
{
  GtkPrintCpdbDispatchWatch *dispatch = data;
  const char *username;
  char *password;
  char *prompt = NULL;
  char *key = NULL;
  char hostname[HTTP_MAX_URI];
  char **auth_info_required;
  char **auth_info_default;
  char **auth_info_display;
  gboolean *auth_info_visible;
  int length = 3;
  int i;

  if (dispatch->backend->authentication_lock)
    return G_SOURCE_REMOVE;

  httpGetHostname (dispatch->request->http, hostname, sizeof (hostname));
  if (is_address_local (hostname))
    strcpy (hostname, "localhost");

  if (dispatch->backend->username != NULL)
    username = dispatch->backend->username;

  auth_info_required = g_new0 (char *, length + 1);
  auth_info_required[0] = g_strdup ("hostname");
  auth_info_required[1] = g_strdup ("username");
  auth_info_required[2] = g_strdup ("password");

  auth_info_default = g_new0 (char *, length + 1);
  auth_info_default[0] = g_strdup (hostname);
  auth_info_default[1] = g_strdup (username);

  auth_info_display = g_new0 (char *, length + 1);
  auth_info_display[1] = g_strdup (_ ("Username:"));
  auth_info_display[2] = g_strdup (_ ("Password:"));

  auth_info_visible = g_new0 (gboolean, length + 1);
  auth_info_visible[1] = TRUE;

  key = g_strconcat (username, "@", hostname, NULL);
  password = g_hash_table_lookup (dispatch->backend->auth, key);

  if (password && dispatch->request->password_state != GTK_CPDB_PASSWORD_NOT_VALID)
    {
      GTK_NOTE (PRINTING,
                g_print ("CPDB: using stored password for %s\n", key));

      overwrite_and_free (dispatch->request->password);
      dispatch->request->password = g_strdup (password);
      g_free (dispatch->request->username);
      dispatch->request->username = g_strdup (username);
      dispatch->request->password_state = GTK_CPDB_PASSWORD_HAS;
    }
  else
    {
      const char *job_title = gtk_cpdb_request_ipp_get_string (dispatch->request, IPP_TAG_NAME, "job-name");

      const char *printer_uri = gtk_cpdb_request_ipp_get_string (dispatch->request, IPP_TAG_URI, "printer-uri");

      char *printer_name = NULL;

      if (printer_uri != NULL && strrchr (printer_uri, '/') != NULL)
        printer_name = g_strdup (strrchr (printer_uri, '/') + 1);

      if (dispatch->request->password_state == GTK_CPDB_PASSWORD_NOT_VALID)
        g_hash_table_remove (dispatch->backend->auth, key);

      dispatch->request->password_state = GTK_CPDB_PASSWORD_REQUESTED;

      dispatch->backend->authentication_lock = TRUE;

      switch ((guint) ippGetOperation (dispatch->request->ipp_request))
        {
        case IPP_PRINT_JOB:
          if (job_title != NULL && printer_name != NULL)
            prompt = g_strdup_printf (_ ("Authentication is required to print document “%s” on printer %s"), job_title, printer_name);
          else
            prompt = g_strdup_printf (_ ("Authentication is required to print a document on %s"), hostname);
          break;
        case IPP_GET_JOB_ATTRIBUTES:
          if (job_title != NULL)
            prompt = g_strdup_printf (_ ("Authentication is required to get attributes of job “%s”"), job_title);
          else
            prompt = g_strdup (_ ("Authentication is required to get attributes of a job"));
          break;
        case IPP_GET_PRINTER_ATTRIBUTES:
          if (printer_name != NULL)
            prompt = g_strdup_printf (_ ("Authentication is required to get attributes of printer %s"), printer_name);
          else
            prompt = g_strdup (_ ("Authentication is required to get attributes of a printer"));
          break;
        // case CPDB_GET_DEFAULT:
        //   prompt = g_strdup_printf (_ ("Authentication is required to get default printer of %s"), hostname);
        //   break;
        // case CPDB_GET_PRINTERS:
        //   prompt = g_strdup_printf (_ ("Authentication is required to get printers from %s"), hostname);
        //   break;
        default:
          /* work around gcc warning about 0 not being a value for this enum */
          if (ippGetOperation (dispatch->request->ipp_request) == 0)
            prompt = g_strdup_printf (_ ("Authentication is required to get a file from %s"), hostname);
          else
            prompt = g_strdup_printf (_ ("Authentication is required on %s"), hostname);
          break;
        }

      g_free (printer_name);

      g_signal_emit_by_name (dispatch->backend, "request-password",
                             auth_info_required, auth_info_default,
                             auth_info_display, auth_info_visible, prompt,
                             FALSE);
      g_free (prompt);
    }

  for (i = 0; i < length; i++)
    {
      g_free (auth_info_required[i]);
      g_free (auth_info_default[i]);
      g_free (auth_info_display[i]);
    }

  g_free (auth_info_required);
  g_free (auth_info_default);
  g_free (auth_info_display);
  g_free (auth_info_visible);
  g_free (key);

  return G_SOURCE_REMOVE;
}

static gboolean
cpdb_dispatch_watch_check (GSource *source)
{
  GtkPrintCpdbDispatchWatch *dispatch;
  GtkCpdbPollState poll_state;
  gboolean result;

  GTK_NOTE (PRINTING,
            g_print ("CPDB: %s <source %p>\n", G_STRFUNC, source));

  dispatch = (GtkPrintCpdbDispatchWatch *) source;

  poll_state = gtk_cpdb_request_get_poll_state (dispatch->request);

  if (poll_state != GTK_CPDB_HTTP_IDLE && !dispatch->request->need_password)
    if (!(dispatch->data_poll->revents & dispatch->data_poll->events))
      return FALSE;

  result = gtk_cpdb_request_read_write (dispatch->request, FALSE);

  if (result && dispatch->data_poll != NULL)
    {
      g_source_remove_poll (source, dispatch->data_poll);
      g_free (dispatch->data_poll);
      dispatch->data_poll = NULL;
    }

  if (dispatch->request->need_password && dispatch->request->password_state != GTK_CPDB_PASSWORD_REQUESTED)
    {
      dispatch->request->need_password = FALSE;
      g_idle_add (request_password, dispatch);
      result = FALSE;
    }

  return result;
}

static gboolean
cpdb_dispatch_watch_prepare (GSource *source,
                             int *timeout_)
{
  GtkPrintCpdbDispatchWatch *dispatch;
  gboolean result;

  dispatch = (GtkPrintCpdbDispatchWatch *) source;

  GTK_NOTE (PRINTING,
            g_print ("CPDB: %s <source %p>\n", G_STRFUNC, source));

  *timeout_ = -1;

  result = gtk_cpdb_request_read_write (dispatch->request, TRUE);

  cpdb_dispatch_add_poll (source);

  return result;
}

static void
cpdb_dispatch_watch_finalize (GSource *source)
{
  GtkPrintCpdbDispatchWatch *dispatch;
  GtkCpdbResult *result;

  GTK_NOTE (PRINTING,
            g_print ("CPDB:  <source %p>\n", G_STRFUNC, source));

  dispatch = (GtkPrintCpdbDispatchWatch *) source;

  result = gtk_cpdb_request_get_result (dispatch->request);
  if (gtk_cpdb_result_get_error_type (result) == GTK_CPDB_ERROR_AUTH)
    {
      const char *username;
      char hostname[HTTP_MAX_URI];
      char *key;

      httpGetHostname (dispatch->request->http, hostname, sizeof (hostname));
      if (is_address_local (hostname))
        strcpy (hostname, "localhost");

      if (dispatch->backend->username != NULL)
        username = dispatch->backend->username;

      key = g_strconcat (username, "@", hostname, NULL);
      GTK_NOTE (PRINTING,
                g_print ("CPDB: removing stored password for %s\n", key));
      g_hash_table_remove (dispatch->backend->auth, key);
      g_free (key);

      if (dispatch->backend)
        dispatch->backend->authentication_lock = FALSE;
    }

  gtk_cpdb_request_free (dispatch->request);

  if (dispatch->backend)
    {
      /* We need to unref this at idle time, because it might be the
       * last reference to this module causing the code to be
       * unloaded (including this particular function!)
       * Update: Doing this at idle caused a deadlock taking the
       * mainloop context lock while being in a GSource callout for
       * multithreaded apps. So, for now we just disable unloading
       * of print backends. See _gtk_print_backend_create for the
       * disabling.
       */

      dispatch->backend->requests = g_list_remove (dispatch->backend->requests, dispatch);

      g_object_unref (dispatch->backend);
      dispatch->backend = NULL;
    }

  if (dispatch->data_poll)
    {
      g_source_remove_poll (source, dispatch->data_poll);
      g_free (dispatch->data_poll);
      dispatch->data_poll = NULL;
    }
}

static GSourceFuncs _cpdb_dispatch_watch_funcs = {
  cpdb_dispatch_watch_prepare,
  cpdb_dispatch_watch_check,
  cpdb_dispatch_watch_dispatch,
  cpdb_dispatch_watch_finalize
};

static void
gtk_printer_cpdb_class_init (GtkPrinterCpdbClass *class)
{
  GObjectClass *object_class = (GObjectClass *) class;

  object_class->finalize = gtk_printer_cpdb_finalize;
  object_class->set_property = gtk_printer_cpdb_set_property;
  object_class->get_property = gtk_printer_cpdb_get_property;

  gtk_printer_cpdb_parent_class = g_type_class_peek_parent (class);

  g_object_class_install_property (G_OBJECT_CLASS (class),
                                   PROP_PROFILE_TITLE,
                                   g_param_spec_string ("profile-title",
                                                        P_ ("Color Profile Title"),
                                                        P_ ("The title of the color profile to use"),
                                                        "",
                                                        G_PARAM_READABLE));
}

static void
gtk_print_backend_cpdb_class_finalize (GtkPrintBackendCpdbClass *class)
{
}

static void
gtk_print_backend_cpdb_init (GtkPrintBackendCpdb *backend)
{
  gtkPrintBackend = GTK_PRINT_BACKEND (backend);
  frontendObj = get_new_FrontendObj (NULL, add_printer_callback, remove_printer_callback);
  connect_to_dbus (frontendObj);
}

static void
cpdb_get_printer_list (GtkPrintBackend *backend)
{
  gtkPrintBackend = GTK_PRINT_BACKEND (backend);
  refresh_printer_list (frontendObj);
}

static void
cpdb_printer_get_settings_from_options (GtkPrinter *printer,
                                        GtkPrinterOptionSet *options,
                                        GtkPrintSettings *settings)
{
  GtkPrinterOption *option;

  option = gtk_printer_option_set_lookup (options, "gtk-n-up");
  if (option)
    gtk_print_settings_set (settings, GTK_PRINT_SETTINGS_NUMBER_UP, option->value);

  option = gtk_printer_option_set_lookup (options, "gtk-n-up-layout");
  if (option)
    gtk_print_settings_set (settings, GTK_PRINT_SETTINGS_NUMBER_UP_LAYOUT, option->value);
}

static GtkPrinterOptionSet *
cpdb_printer_get_options (GtkPrinter *printer,
                          GtkPrintSettings *settings,
                          GtkPageSetup *page_setup,
                          GtkPrintCapabilities capabilities)
{
  GtkPrinterOption *option;
  GtkPrinterOptionSet *set = gtk_printer_option_set_new ();

  // CPDB Option CPD_OPTION_NUMBER_UP
  const char *n_up[] = { "1", "2", "4", "6", "9", "16" };
  option = gtk_printer_option_new ("gtk-n-up", C_ ("printer option", "Pages per Sheet"), GTK_PRINTER_OPTION_TYPE_PICKONE);
  gtk_printer_option_choices_from_array (option, G_N_ELEMENTS (n_up), n_up, n_up);
  gtk_printer_option_set (option, "1");
  gtk_printer_option_set_add (set, option);

  // CPDB Option CPD_OPTION_JOB_PRIORITY
  const char *priority[] = { "100", "80", "50", "30" };
  const char *priority_display[] = { N_ ("Urgent"), N_ ("High"), N_ ("Medium"), N_ ("Low") };
  option = gtk_printer_option_new ("gtk-job-prio", _ ("Job Priority"), GTK_PRINTER_OPTION_TYPE_PICKONE);
  gtk_printer_option_choices_from_array (option, G_N_ELEMENTS (priority), priority, priority_display);
  gtk_printer_option_set (option, "50");
  gtk_printer_option_set_add (set, option);
  g_object_unref (option);

  // CPDB Option CPD_OPTION_SIDES
  const char *sides[] = { "one-sided", "two-sided-short", "two-sided-long" };
  const char *sides_display[] = { N_ ("One Sided"), N_ ("Long Edged (Standard)"), N_ ("Short Edged (Flip)") };
  option = gtk_printer_option_new ("gtk-duplex", _ ("Duplex Printing"), GTK_PRINTER_OPTION_TYPE_PICKONE);
  gtk_printer_option_choices_from_array (option, G_N_ELEMENTS (sides), sides, sides_display);
  gtk_printer_option_set (option, "one-sided");
  gtk_printer_option_set_add (set, option);
  g_object_unref (option);

  return set;
}

static void
cpdb_printer_prepare_for_print (GtkPrinter *printer,
                                GtkPrintJob *print_job,
                                GtkPrintSettings *settings,
                                GtkPageSetup *page_setup)
{
  double scale;
  GtkPrintPages pages;
  GtkPageRange *ranges;
  GtkPageSet page_set;
  GtkPaperSize *paper_size;
  const char *ppd_paper_name;
  int n_ranges;

  pages = gtk_print_settings_get_print_pages (settings);
  gtk_print_job_set_pages (print_job, pages);
  if (pages == GTK_PRINT_PAGES_RANGES)
    ranges = gtk_print_settings_get_page_ranges (settings, &n_ranges);
  else
    {
      ranges = NULL;
      n_ranges = 0;
    }
  gtk_print_job_set_page_ranges (print_job, ranges, n_ranges);

  gtk_print_job_set_collate (print_job, gtk_print_settings_get_collate (settings));
  gtk_print_job_set_reverse (print_job, gtk_print_settings_get_reverse (settings));
  gtk_print_job_set_num_copies (print_job, gtk_print_settings_get_n_copies (settings));
  gtk_print_job_set_n_up (print_job, gtk_print_settings_get_number_up (settings));
  gtk_print_job_set_n_up_layout (print_job, gtk_print_settings_get_number_up_layout (settings));
  gtk_print_job_set_rotate (print_job, TRUE);

  scale = gtk_print_settings_get_scale (settings);
  if (scale != 100.0)
    gtk_print_job_set_scale (print_job, scale / 100.0);

  page_set = gtk_print_settings_get_page_set (settings);
  if (page_set == GTK_PAGE_SET_EVEN)
    gtk_print_settings_set (settings, "cpdb-page-set", "even");
  else if (page_set == GTK_PAGE_SET_ODD)
    gtk_print_settings_set (settings, "cpdb-page-set", "odd");
  gtk_print_job_set_page_set (print_job, GTK_PAGE_SET_ALL);

  paper_size = gtk_page_setup_get_paper_size (page_setup);
  ppd_paper_name = gtk_paper_size_get_ppd_name (paper_size);

  if (ppd_paper_name != NULL)
    gtk_print_settings_set (settings, "cpdb-PageSize", ppd_paper_name);
  else if (gtk_paper_size_is_ipp (paper_size))
    gtk_print_settings_set (settings, "cpdb-media", gtk_paper_size_get_name (paper_size));
  else
    {
      char width[G_ASCII_DTOSTR_BUF_SIZE];
      char height[G_ASCII_DTOSTR_BUF_SIZE];
      char *custom_name;

      g_ascii_formatd (width, sizeof (width), "%.2f", gtk_paper_size_get_width (paper_size, GTK_UNIT_POINTS));
      g_ascii_formatd (height, sizeof (height), "%.2f", gtk_paper_size_get_height (paper_size, GTK_UNIT_POINTS));
      custom_name = g_strdup_printf (("Custom.%sx%s"), width, height);
      gtk_print_settings_set (settings, "cpdb-PageSize", custom_name);
      g_free (custom_name);
    }

  if (gtk_print_settings_get_number_up (settings) > 1)
    {
      GtkNumberUpLayout layout = gtk_print_settings_get_number_up_layout (settings);
      GEnumClass *enum_class;
      GEnumValue *enum_value;

      switch (gtk_page_setup_get_orientation (page_setup))
        {
        case GTK_PAGE_ORIENTATION_LANDSCAPE:
          if (layout < 4)
            layout = layout + 2 + 4 * (1 - layout / 2);
          else
            layout = layout - 3 - 2 * (layout % 2);
          break;
        case GTK_PAGE_ORIENTATION_REVERSE_PORTRAIT:
          layout = (layout + 3 - 2 * (layout % 2)) % 4 + 4 * (layout / 4);
          break;
        case GTK_PAGE_ORIENTATION_REVERSE_LANDSCAPE:
          if (layout < 4)
            layout = layout + 5 - 2 * (layout % 2);
          else
            layout = layout - 6 + 4 * (1 - (layout - 4) / 2);
          break;

        case GTK_PAGE_ORIENTATION_PORTRAIT:
        default:
          break;
        }

      enum_class = g_type_class_ref (GTK_TYPE_NUMBER_UP_LAYOUT);
      enum_value = g_enum_get_value (enum_class, layout);
      gtk_print_settings_set (settings, "cpdb-number-up-layout", enum_value->value_nick);
      g_type_class_unref (enum_class);
    }
}

static void
cpdb_free_print_stream_data (CpdbPrintStreamData *data)
{
  GTK_NOTE (PRINTING,
            g_print ("CPDB: %s\n", G_STRFUNC));

  if (data->dnotify)
    data->dnotify (data->user_data);
  g_object_unref (data->job);
  if (data->http != NULL)
    httpClose (data->http);
  g_free (data);
}

static void
gtk_cpdb_result_free (GtkCpdbResult *result)
{
  g_free (result->error_msg);

  if (result->ipp_response)
    ippDelete (result->ipp_response);

  g_free (result);
}

#define _GTK_CPDB_MAX_ATTEMPTS 10

static void
cpdb_dispatch_add_poll (GSource *source)
{
  GtkPrintCpdbDispatchWatch *dispatch;
  GtkCpdbPollState poll_state;

  dispatch = (GtkPrintCpdbDispatchWatch *) source;

  poll_state = gtk_cpdb_request_get_poll_state (dispatch->request);

  /* Remove the old source if the poll state changed. */
  if (poll_state != dispatch->poll_state && dispatch->data_poll != NULL)
    {
      g_source_remove_poll (source, dispatch->data_poll);
      g_free (dispatch->data_poll);
      dispatch->data_poll = NULL;
    }

  if (dispatch->request->http != NULL)
    {
      if (dispatch->data_poll == NULL)
        {
          dispatch->data_poll = g_new0 (GPollFD, 1);
          dispatch->poll_state = poll_state;

          if (poll_state == GTK_CPDB_HTTP_READ)
            dispatch->data_poll->events = G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_PRI;
          else if (poll_state == GTK_CPDB_HTTP_WRITE)
            dispatch->data_poll->events = G_IO_OUT | G_IO_ERR;
          else
            dispatch->data_poll->events = 0;

          dispatch->data_poll->fd = httpGetFd (dispatch->request->http);
          g_source_add_poll (source, dispatch->data_poll);
        }
    }
}

static void
cpdb_request_execute (GtkPrintBackendCpdb *print_backend,
                      GtkCpdbRequest *request,
                      GtkPrintCpdbResponseCallbackFunc callback,
                      gpointer user_data,
                      GDestroyNotify notify)
{
  GtkPrintCpdbDispatchWatch *dispatch;

  dispatch = (GtkPrintCpdbDispatchWatch *) g_source_new (&_cpdb_dispatch_watch_funcs,
                                                         sizeof (GtkPrintCpdbDispatchWatch));
  g_source_set_name (&dispatch->source, "GTK CPDB backend");

  GTK_NOTE (PRINTING,
            g_print (" CPDB: %s <source %p> - Executing cpdb request on server '%s' and resource '%s'\n", G_STRFUNC, dispatch, request->server, request->resource));

  dispatch->request = request;
  dispatch->backend = g_object_ref (print_backend);
  dispatch->poll_state = GTK_CPDB_HTTP_IDLE;
  dispatch->data_poll = NULL;
  dispatch->callback = NULL;
  dispatch->callback_data = NULL;

  print_backend->requests = g_list_prepend (print_backend->requests, dispatch);

  g_source_set_callback ((GSource *) dispatch, (GSourceFunc) callback, user_data, notify);

  if (request->need_auth_info)
    {
      dispatch->callback = callback;
      dispatch->callback_data = user_data;
      lookup_auth_info (dispatch);
    }
  else
    {
      g_source_attach ((GSource *) dispatch, NULL);
      g_source_unref ((GSource *) dispatch);
    }
}

static cairo_surface_t *
cpdb_printer_create_cairo_surface (GtkPrinter *printer,
                                   GtkPrintSettings *settings,
                                   double width,
                                   double height,
                                   GIOChannel *cache_io)
{
  cairo_surface_t *surface;
  return surface;
}

static void
add_cpdb_options (const char *key,
                  const char *value,
                  gpointer user_data)
{
  CpdbOptionsData *data = (CpdbOptionsData *) user_data;
  GtkCpdbRequest *request = data->request;
  GtkPrinterCpdb *printer = data->printer;
  gboolean custom_value = FALSE;
  char *new_value = NULL;
  int i;

  if (!key || !value)
    return;

  if (!g_str_has_prefix (key, "cpdb-"))
    return;

  if (strcmp (value, "gtk-ignore-value") == 0)
    return;

  key = key + strlen ("cpdb-");

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS

  if (printer && printer->ppd_file && !g_str_has_prefix (value, "Custom."))
    {
      ppd_coption_t *coption;
      gboolean found = FALSE;
      gboolean custom_values_enabled = FALSE;

      coption = ppdFindCustomOption (printer->ppd_file, key);
      if (coption && coption->option)
        {
          for (i = 0; i < coption->option->num_choices; i++)
            {
              /* Are custom values enabled ? */
              if (g_str_equal (coption->option->choices[i].choice, "Custom"))
                custom_values_enabled = TRUE;

              /* Is the value among available choices ? */
              if (g_str_equal (coption->option->choices[i].choice, value))
                found = TRUE;
            }

          if (custom_values_enabled && !found)
            {
              /* Check syntax of the invalid choice to see whether
                 it could be a custom value */
              if (g_str_equal (key, "PageSize") ||
                  g_str_equal (key, "PageRegion"))
                {
                  /* Handle custom page sizes... */
                  if (g_regex_match_simple ("^" UNSIGNED_FLOAT_REGEX "x" UNSIGNED_FLOAT_REGEX "(cm|mm|m|in|ft|pt)?$", value, G_REGEX_CASELESS, 0))
                    custom_value = TRUE;
                  else
                    {
                      if (data->page_setup != NULL)
                        {
                          custom_value = TRUE;
                          new_value =
                              g_strdup_printf ("Custom.%.2fx%.2fmm",
                                               gtk_paper_size_get_width (gtk_page_setup_get_paper_size (data->page_setup), GTK_UNIT_MM),
                                               gtk_paper_size_get_height (gtk_page_setup_get_paper_size (data->page_setup), GTK_UNIT_MM));
                        }
                    }
                }
              else
                {
                  /* Handle other custom options... */
                  ppd_cparam_t *cparam;

                  cparam = (ppd_cparam_t *) cupsArrayFirst (coption->params); // need to figure out an alternative, might just work though
                  if (cparam != NULL)
                    {
                      switch (cparam->type)
                        {
                        case PPD_CUSTOM_CURVE:
                        case PPD_CUSTOM_INVCURVE:
                        case PPD_CUSTOM_REAL:
                          if (g_regex_match_simple ("^" SIGNED_FLOAT_REGEX "$", value, G_REGEX_CASELESS, 0))
                            custom_value = TRUE;
                          break;

                        case PPD_CUSTOM_POINTS:
                          if (g_regex_match_simple ("^" SIGNED_FLOAT_REGEX "(cm|mm|m|in|ft|pt)?$", value, G_REGEX_CASELESS, 0))
                            custom_value = TRUE;
                          break;

                        case PPD_CUSTOM_INT:
                          if (g_regex_match_simple ("^" SIGNED_INTEGER_REGEX "$", value, G_REGEX_CASELESS, 0))
                            custom_value = TRUE;
                          break;

                        case PPD_CUSTOM_PASSCODE:
                        case PPD_CUSTOM_PASSWORD:
                        case PPD_CUSTOM_STRING:
                          custom_value = TRUE;
                          break;

                          // #if (CPDB_VERSION_MAJOR >= 3) ||                            \
                          //     (CPDB_VERSION_MAJOR == 2 && CPDB_VERSION_MINOR >= 3) || \
                          //     (CPDB_VERSION_MAJOR == 2 && CPDB_VERSION_MINOR == 2 && CPDB_VERSION_PATCH >= 12)
                          //                         case PPD_CUSTOM_UNKNOWN:
                          // #endif

                        default:
                          custom_value = FALSE;
                        }
                    }
                }
            }
        }
    }

  G_GNUC_END_IGNORE_DEPRECATIONS

  /* Add "Custom." prefix to custom values if not already added. */
  if (custom_value)
    {
      if (new_value == NULL)
        new_value = g_strdup_printf ("Custom.%s", value);
      gtk_cpdb_request_encode_option (request, key, new_value);
      g_free (new_value);
    }
  else
    gtk_cpdb_request_encode_option (request, key, value);
}

typedef struct
{
  GtkPrintBackendCpdb *print_backend;
  GtkPrintJob *job;
  int job_id;
  int counter;
} CpdbJobPollData;

static void
cpdb_request_job_info (CpdbJobPollData *data);

static void
job_object_died (gpointer user_data,
                 GObject *where_the_object_was)
{
  CpdbJobPollData *data = user_data;
  data->job = NULL;
}

static void
cpdb_job_poll_data_free (CpdbJobPollData *data)
{
  if (data->job)
    g_object_weak_unref (G_OBJECT (data->job), job_object_died, data);

  g_free (data);
}

static gboolean
cpdb_job_info_poll_timeout (gpointer user_data)
{
  CpdbJobPollData *data = user_data;

  if (data->job == NULL)
    cpdb_job_poll_data_free (data);
  else
    cpdb_request_job_info (data);

  return G_SOURCE_REMOVE;
}

static void
cpdb_request_job_info_cb (GtkPrintBackendCpdb *print_backend,
                          GtkCpdbResult *result,
                          gpointer user_data)
{
  CpdbJobPollData *data = user_data;
  ipp_attribute_t *attr;
  ipp_t *response;
  int state;
  gboolean done;

  if (data->job == NULL)
    {
      cpdb_job_poll_data_free (data);
      return;
    }

  data->counter++;

  response = gtk_cpdb_result_get_response (result);

  attr = ippFindAttribute (response, "job-state", IPP_TAG_ENUM);
  state = ippGetInteger (attr, 0);

  done = FALSE;
  switch (state)
    {
    case IPP_JOB_PENDING:
    case IPP_JOB_HELD:
    case IPP_JOB_STOPPED:
      gtk_print_job_set_status (data->job, GTK_PRINT_STATUS_PENDING);
      break;
    case IPP_JOB_PROCESSING:
      gtk_print_job_set_status (data->job, GTK_PRINT_STATUS_PRINTING);
      break;
    default:
    case IPP_JOB_CANCELLED:
    case IPP_JOB_ABORTED:
      gtk_print_job_set_status (data->job, GTK_PRINT_STATUS_FINISHED_ABORTED);
      done = TRUE;
      break;
    case 0:
    case IPP_JOB_COMPLETED:
      gtk_print_job_set_status (data->job, GTK_PRINT_STATUS_FINISHED);
      done = TRUE;
      break;
    }

  if (!done && data->job != NULL)
    {
      guint32 timeout;
      guint id;

      if (data->counter < 5)
        timeout = 100;
      else if (data->counter < 10)
        timeout = 500;
      else
        timeout = 1000;

      id = g_timeout_add (timeout, cpdb_job_info_poll_timeout, data);
      g_source_set_name_by_id (id, "[gtk] cpdb_job_info_poll_timeout");
    }
  else
    cpdb_job_poll_data_free (data);
}

static void
cpdb_request_job_info (CpdbJobPollData *data)
{
  GtkCpdbRequest *request;
  char *job_uri;

  request = gtk_cpdb_request_new_with_username (NULL,
                                                GTK_CPDB_POST,
                                                IPP_GET_JOB_ATTRIBUTES,
                                                NULL,
                                                NULL,
                                                NULL,
                                                data->print_backend->username);

  job_uri = g_strdup_printf ("ipp://localhost/jobs/%d", data->job_id);
  gtk_cpdb_request_ipp_add_string (request, IPP_TAG_OPERATION, IPP_TAG_URI,
                                   "job-uri", NULL, job_uri);
  g_free (job_uri);

  cpdb_request_execute (data->print_backend,
                        request,
                        (GtkPrintCpdbResponseCallbackFunc) cpdb_request_job_info_cb,
                        data,
                        NULL);
}

static void
cpdb_begin_polling_info (GtkPrintBackendCpdb *print_backend,
                         GtkPrintJob *job,
                         int job_id)
{
  CpdbJobPollData *data;

  data = g_new0 (CpdbJobPollData, 1);

  data->print_backend = print_backend;
  data->job = job;
  data->job_id = job_id;
  data->counter = 0;

  g_object_weak_ref (G_OBJECT (job), job_object_died, data);

  cpdb_request_job_info (data);
}

static void
cpdb_print_cb (GtkPrintBackendCpdb *print_backend,
               GtkCpdbResult *result,
               gpointer user_data)
{
  GError *error = NULL;
  CpdbPrintStreamData *ps = user_data;

  GTK_NOTE (PRINTING,
            g_print ("CPDB: %s\n", G_STRFUNC));

  if (gtk_cpdb_result_is_error (result))
    error = g_error_new_literal (gtk_print_error_quark (),
                                 GTK_PRINT_ERROR_INTERNAL_ERROR,
                                 gtk_cpdb_result_get_error_string (result));

  if (ps->callback)
    ps->callback (ps->job, ps->user_data, error);

  if (error == NULL)
    {
      int job_id = 0;
      ipp_attribute_t *attr; /* IPP job-id attribute */
      ipp_t *response = gtk_cpdb_result_get_response (result);

      if ((attr = ippFindAttribute (response, "job-id", IPP_TAG_INTEGER)) != NULL)
        job_id = ippGetInteger (attr, 0);

      if (!gtk_print_job_get_track_print_status (ps->job) || job_id == 0)
        gtk_print_job_set_status (ps->job, GTK_PRINT_STATUS_FINISHED);
      else
        {
          gtk_print_job_set_status (ps->job, GTK_PRINT_STATUS_PENDING);
          cpdb_begin_polling_info (print_backend, ps->job, job_id);
        }
    }
  else
    gtk_print_job_set_status (ps->job, GTK_PRINT_STATUS_FINISHED_ABORTED);

  if (error)
    g_error_free (error);
}

static void
gtk_print_backend_cpdb_print_stream (GtkPrintBackend *print_backend,
                                     GtkPrintJob *job,
                                     GIOChannel *data_io,
                                     GtkPrintJobCompleteFunc callback,
                                     gpointer user_data,
                                     GDestroyNotify dnotify)
{
  GtkPrinterCpdb *cpdb_printer;
  CpdbPrintStreamData *ps;
  CpdbOptionsData *options_data;
  GtkPageSetup *page_setup;
  GtkCpdbRequest *request = NULL;
  GtkPrintSettings *settings;
  const char *title;
  char printer_absolute_uri[HTTP_MAX_URI];
  http_t *http = NULL;

  GTK_NOTE (PRINTING,
            g_print ("CPDB: %s\n", G_STRFUNC));

  cpdb_printer = GTK_PRINTER_CPDB (gtk_print_job_get_printer (job));

  settings = gtk_print_job_get_settings (job);

  if (cpdb_printer->avahi_browsed)
    {
      http = httpConnect2 (cpdb_printer->hostname, cpdb_printer->port,
                           NULL, AF_UNSPEC,
                           HTTP_ENCRYPTION_IF_REQUESTED,
                           1, 30000,
                           NULL);

      if (http)
        {
          request = gtk_cpdb_request_new_with_username (http,
                                                        GTK_CPDB_POST,
                                                        IPP_PRINT_JOB,
                                                        data_io,
                                                        cpdb_printer->hostname,
                                                        cpdb_printer->device_uri,
                                                        GTK_PRINT_BACKEND_CPDB (print_backend)->username);
          g_snprintf (printer_absolute_uri, HTTP_MAX_URI, "%s", cpdb_printer->printer_uri);
        }
      else
        {
          GError *error = NULL;

          GTK_NOTE (PRINTING,
                    g_warning ("CPDB: Error connecting to %s:%d",
                               cpdb_printer->hostname,
                               cpdb_printer->port));

          error = g_error_new (gtk_print_error_quark (),
                               GTK_CPDB_ERROR_GENERAL,
                               "Error connecting to %s",
                               cpdb_printer->hostname);

          gtk_print_job_set_status (job, GTK_PRINT_STATUS_FINISHED_ABORTED);

          if (callback)
            {
              callback (job, user_data, error);
            }

          g_clear_error (&error);

          return;
        }
    }
  else
    {
      request = gtk_cpdb_request_new_with_username (NULL,
                                                    GTK_CPDB_POST,
                                                    IPP_PRINT_JOB,
                                                    data_io,
                                                    NULL,
                                                    cpdb_printer->device_uri,
                                                    GTK_PRINT_BACKEND_CPDB (print_backend)->username);

      httpAssembleURIf (HTTP_URI_CODING_ALL,
                        printer_absolute_uri,
                        sizeof (printer_absolute_uri),
                        "ipp",
                        NULL,
                        "localhost",
                        ippPort (),
                        "/printers/%s",
                        gtk_printer_get_name (gtk_print_job_get_printer (job)));
    }

  gtk_cpdb_request_set_ipp_version (request,
                                    cpdb_printer->ipp_version_major,
                                    cpdb_printer->ipp_version_minor);

  gtk_cpdb_request_ipp_add_string (request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, printer_absolute_uri);

  title = gtk_print_job_get_title (job);
  if (title)
    {
      char *title_truncated = NULL;
      size_t title_bytes = strlen (title);

      if (title_bytes >= IPP_MAX_NAME)
        {
          char *end;

          end = g_utf8_find_prev_char (title, title + IPP_MAX_NAME - 1);
          title_truncated = g_utf8_substring (title,
                                              0,
                                              g_utf8_pointer_to_offset (title, end));
        }

      gtk_cpdb_request_ipp_add_string (request, IPP_TAG_OPERATION,
                                       IPP_TAG_NAME, "job-name",
                                       NULL,
                                       title_truncated ? title_truncated : title);
      g_free (title_truncated);
    }

  g_object_get (job,
                "page-setup",
                &page_setup,
                NULL);

  options_data = g_new0 (CpdbOptionsData, 1);
  options_data->request = request;
  options_data->printer = cpdb_printer;
  options_data->page_setup = page_setup;
  gtk_print_settings_foreach (settings, add_cpdb_options, options_data);
  g_clear_object (&page_setup);
  g_free (options_data);

  ps = g_new0 (CpdbPrintStreamData, 1);
  ps->callback = callback;
  ps->user_data = user_data;
  ps->dnotify = dnotify;
  ps->job = g_object_ref (job);
  ps->http = http;

  request->need_auth_info = FALSE;
  request->auth_info_required = NULL;

  /* Check if auth_info_required is set and if it should be handled.
      * The libraries handle the ticket exchange for "negotiate". */

  if (cpdb_printer->auth_info_required != NULL &&
      g_strv_length (cpdb_printer->auth_info_required) == 1 &&
      g_strcmp0 (cpdb_printer->auth_info_required[0], "negotiate") == 0)
    {
      GTK_NOTE (PRINTING,
                g_print ("CPDB: Ignoring auth-info-required \"%s\"\n",
                         cpdb_printer->auth_info_required[0]));
    }
  else if (cpdb_printer->auth_info_required != NULL)
    {
      request->need_auth_info = TRUE;
      request->auth_info_required = g_strdupv (cpdb_printer->auth_info_required);
    }

  cpdb_request_execute (GTK_PRINT_BACKEND_CPDB (print_backend),
                        request,
                        (GtkPrintCpdbResponseCallbackFunc) cpdb_print_cb,
                        ps,
                        (GDestroyNotify) cpdb_free_print_stream_data);
}

static GtkPrinter *
get_gtk_printer_from_printer_obj (PrinterObj *p)
{
  GtkPrinter *printer;

  printer = g_object_new (GTK_TYPE_PRINTER,
                          "name", p->name,
                          "backend", GTK_PRINT_BACKEND_CPDB (gtkPrintBackend));

  gtk_printer_set_icon_name (printer, "printer");
  gtk_printer_set_state_message (printer, p->state);
  gtk_printer_set_location (printer, p->location);
  gtk_printer_set_description (printer, p->info);
  gtk_printer_set_is_accepting_jobs (printer, p->is_accepting_jobs);
  gtk_printer_set_job_count (printer, get_active_jobs_count (p));

  gtk_printer_set_has_details (printer, TRUE);
  gtk_printer_set_is_active (printer, TRUE);
  // // Given GCP is going to be deprecated, the CPDB default printer wil be overall default.
  // if (strcmp(p->backend_name, "CPDB") == 0 &&
  //     strcmp(get_default_printer(frontendObj, p->backend_name), p->name) == 0)
  //   {
  //     gtk_printer_set_is_default (printer, TRUE);
  //   }
  return printer;
}

static int
add_printer_callback (PrinterObj *p)
{
  GtkPrinter *printer = get_gtk_printer_from_printer_obj (p);
  gtk_print_backend_add_printer (gtkPrintBackend, printer);
  g_object_unref (printer);
  gtk_print_backend_set_list_done (gtkPrintBackend);

  return 0;
}

static int
remove_printer_callback (PrinterObj *p)
{
  g_message ("Removed Printer %s : %s!\n", p->name, p->backend_name);
  return 0;
}
