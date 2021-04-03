/* add license */

#include "config.h"

#include <glib/gi18n-lib.h>

#ifdef HAVE_COLORD
#include <colord.h>
#endif

#include "gtkintl.h"
#include "gtkprintercpdb.h"

enum
{
  PROP_0,
  PROP_PROFILE_TITLE
};

static GtkPrinterClass *gtk_printer_cpdb_parent_class;
static GType gtk_printer_cpdb_type = 0;

void
gtk_printer_cpdb_register_type (GTypeModule *module)
{
  const GTypeInfo object_info = {
    sizeof (GtkPrinterCpdbClass),
    (GBaseInitFunc) NULL,
    (GBaseFinalizeFunc) NULL,
    (GClassInitFunc) gtk_printer_cpdb_class_init,
    NULL, /* class_finalize */
    NULL, /* class_data */
    sizeof (GtkPrinterCpdb),
    0, /* n_preallocs */
    (GInstanceInitFunc) gtk_printer_cpdb_init,
  };

  gtk_printer_cpdb_type = g_type_module_register_type (module,
                                                       GTK_TYPE_PRINTER,
                                                       "GtkPrinterCpdb",
                                                       &object_info, 0);
}

GType
gtk_printer_cpdb_get_type (void)
{
  return gtk_printer_cpdb_type;
}

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
gtk_printer_cpdb_init (GtkPrinterCpdb *printer)
{
  printer->device_uri = NULL;
  printer->original_device_uri = NULL;
  printer->printer_uri = NULL;
  printer->state = 0;
  printer->hostname = NULL;
  printer->port = 0;
  printer->original_hostname = NULL;
  printer->original_resource = NULL;
  printer->original_port = 0;
  printer->request_original_uri = FALSE;
  printer->ppd_name = NULL;
  printer->ppd_file = NULL;
  printer->default_cover_before = NULL;
  printer->default_cover_after = NULL;
  printer->remote = FALSE;
  printer->get_remote_ppd_poll = 0;
  printer->get_remote_ppd_attempts = 0;
  printer->remote_cpdb_connection_test = NULL;
  printer->auth_info_required = NULL;
  printer->default_number_up = 1;
  printer->avahi_browsed = FALSE;
  printer->avahi_name = NULL;
  printer->avahi_type = NULL;
  printer->avahi_domain = NULL;
  printer->ipp_version_major = 1;
  printer->ipp_version_minor = 1;
  printer->supports_copies = FALSE;
  printer->supports_collate = FALSE;
  printer->supports_number_up = FALSE;
  printer->media_default = NULL;
  printer->media_supported = NULL;
  printer->media_size_supported = NULL;
  printer->media_bottom_margin_default = 0;
  printer->media_top_margin_default = 0;
  printer->media_left_margin_default = 0;
  printer->media_right_margin_default = 0;
  printer->media_margin_default_set = FALSE;
  printer->sides_default = NULL;
  printer->sides_supported = NULL;
  printer->number_of_covers = 0;
  printer->covers = NULL;
  printer->output_bin_default = NULL;
  printer->output_bin_supported = NULL;
}

static void
gtk_printer_cpdb_finalize (GObject *object)
{
  GtkPrinterCpdb *printer;

  g_return_if_fail (object != NULL);

  printer = GTK_PRINTER_CPDB (object);

  g_free (printer->device_uri);
  g_free (printer->original_device_uri);
  g_free (printer->printer_uri);
  g_free (printer->hostname);
  g_free (printer->original_hostname);
  g_free (printer->original_resource);
  g_free (printer->ppd_name);
  g_free (printer->default_cover_before);
  g_free (printer->default_cover_after);
  g_strfreev (printer->auth_info_required);

#ifdef HAVE_COLORD
  if (printer->colord_cancellable)
    {
      g_cancellable_cancel (printer->colord_cancellable);
      g_object_unref (printer->colord_cancellable);
    }
  g_free (printer->colord_title);
  g_free (printer->colord_qualifier);
  if (printer->colord_client)
    g_object_unref (printer->colord_client);
  if (printer->colord_device)
    g_object_unref (printer->colord_device);
  if (printer->colord_profile)
    g_object_unref (printer->colord_profile);
#endif

  g_free (printer->avahi_name);
  g_free (printer->avahi_type);
  g_free (printer->avahi_domain);

  g_strfreev (printer->covers);

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS

  if (printer->ppd_file)
    ppdClose (printer->ppd_file);

  G_GNUC_END_IGNORE_DEPRECATIONS

  g_free (printer->media_default);
  g_list_free_full (printer->media_supported, g_free);
  g_list_free_full (printer->media_size_supported, g_free);

  g_free (printer->sides_default);
  g_list_free_full (printer->sides_supported, g_free);

  g_free (printer->output_bin_default);
  g_list_free_full (printer->output_bin_supported, g_free);

  if (printer->get_remote_ppd_poll > 0)
    g_source_remove (printer->get_remote_ppd_poll);
  printer->get_remote_ppd_attempts = 0;

  gtk_cpdb_connection_test_free (printer->remote_cpdb_connection_test);

  G_OBJECT_CLASS (gtk_printer_cpdb_parent_class)->finalize (object);
}

static void
gtk_printer_cpdb_set_property (GObject *object,
                               guint prop_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gtk_printer_cpdb_get_property (GObject *object,
                               guint prop_id,
                               GValue *value,
                               GParamSpec *pspec)
{
#ifdef HAVE_COLORD
  GtkPrinterCpdb *printer = GTK_PRINTER_CPDB (object);
#endif

  switch (prop_id)
    {
    case PROP_PROFILE_TITLE:
#ifdef HAVE_COLORD
      if (printer->colord_title)
        g_value_set_string (value, printer->colord_title);
      else
        g_value_set_static_string (value, "");
#else
      g_value_set_static_string (value, NULL);
#endif
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

#ifdef HAVE_COLORD

static void
colord_update_ui_from_settings (GtkPrinterCpdb *printer)
{
  const char *title = NULL;

  /* not yet connected to colord */
  if (printer->colord_client == NULL)
    goto out;
  if (!cd_client_get_connected (printer->colord_client))
    goto out;

  /* failed to get a colord device for the printer */
  if (printer->colord_device == NULL)
    {
      /* TRANSLATORS: when we're running an old CPDB, and
       * it hasn't registered the device with colord */
      title = _ ("Color management unavailable");
      goto out;
    }

  /* when colord prevents us from connecting (should not happen) */
  if (!cd_device_get_connected (printer->colord_device))
    goto out;

  /* failed to get a colord device for the printer */
  if (printer->colord_profile == NULL)
    {
      /* TRANSLATORS: when there is no color profile available */
      title = _ ("No profile available");
      goto out;
    }

  /* when colord prevents us from connecting (should not happen) */
  if (!cd_profile_get_connected (printer->colord_profile))
    goto out;
  title = cd_profile_get_title (printer->colord_profile);
  if (title == NULL)
    {
      /* TRANSLATORS: when the color profile has no title */
      title = _ ("Unspecified profile");
      goto out;
    }

out:
  /* SUCCESS! */
  if (g_strcmp0 (title, printer->colord_title) != 0)
    {
      g_free (printer->colord_title);
      printer->colord_title = g_strdup (title);
      g_object_notify (G_OBJECT (printer), "profile-title");
    }
  return;
}

static void
colord_client_profile_connect_cb (GObject *source_object,
                                  GAsyncResult *res,
                                  gpointer user_data)
{
  gboolean ret;
  GError *error = NULL;
  GtkPrinterCpdb *printer = GTK_PRINTER_CPDB (user_data);

  ret = cd_profile_connect_finish (CD_PROFILE (source_object),
                                   res,
                                   &error);
  if (!ret)
    {
      g_warning ("failed to get properties from the profile: %s",
                 error->message);
      g_error_free (error);
    }

  /* update the UI */
  colord_update_ui_from_settings (printer);

  g_object_unref (printer);
}

static void
colord_client_device_get_profile_for_qualifiers_cb (GObject *source_object,
                                                    GAsyncResult *res,
                                                    gpointer user_data)
{
  GtkPrinterCpdb *printer = GTK_PRINTER_CPDB (user_data);
  GError *error = NULL;

  printer->colord_profile = cd_device_get_profile_for_qualifiers_finish (printer->colord_device,
                                                                         res,
                                                                         &error);
  if (printer->colord_profile == NULL)
    {
      /* not having a profile for a qualifier is not a warning */
      g_debug ("no profile for device %s: %s",
               cd_device_get_id (printer->colord_device),
               error->message);
      g_error_free (error);
      goto out;
    }

  /* get details about the profile */
  cd_profile_connect (printer->colord_profile,
                      printer->colord_cancellable,
                      colord_client_profile_connect_cb,
                      g_object_ref (printer));
out:
  /* update the UI */
  colord_update_ui_from_settings (printer);

  g_object_unref (printer);
}

void
gtk_printer_cpdb_update_settings (GtkPrinterCpdb *printer,
                                  GtkPrintSettings *settings,
                                  GtkPrinterOptionSet *set)
{
  char *qualifier = NULL;
  char **qualifiers = NULL;
  GtkPrinterOption *option;
  const char *format[3];

  /* nothing set yet */
  if (printer->colord_device == NULL)
    goto out;
  if (!cd_device_get_connected (printer->colord_device))
    goto out;

  /* cpdbICCQualifier1 */
  option = gtk_printer_option_set_lookup (set, "cpdb-ColorSpace");
  if (option == NULL)
    option = gtk_printer_option_set_lookup (set, "cpdb-ColorModel");
  if (option != NULL)
    format[0] = option->value;
  else
    format[0] = "*";

  /* cpdbICCQualifier2 */
  option = gtk_printer_option_set_lookup (set, "cpdb-OutputMode");
  if (option != NULL)
    format[1] = option->value;
  else
    format[1] = "*";

  /* cpdbICCQualifier3 */
  option = gtk_printer_option_set_lookup (set, "cpdb-Resolution");
  if (option != NULL)
    format[2] = option->value;
  else
    format[2] = "*";

  /* get profile for the device given the qualifier */
  qualifier = g_strdup_printf ("%s.%s.%s,%s.%s.*,%s.*.*",
                               format[0], format[1], format[2],
                               format[0], format[1],
                               format[0]);

  /* only requery colord if the option that was changed would give
   * us a different profile result */
  if (g_strcmp0 (qualifier, printer->colord_qualifier) == 0)
    goto out;

  qualifiers = g_strsplit (qualifier, ",", -1);
  cd_device_get_profile_for_qualifiers (printer->colord_device,
                                        (const char **) qualifiers,
                                        printer->colord_cancellable,
                                        colord_client_device_get_profile_for_qualifiers_cb,
                                        g_object_ref (printer));

  /* save for the future */
  g_free (printer->colord_qualifier);
  printer->colord_qualifier = g_strdup (qualifier);

  /* update the UI */
  colord_update_ui_from_settings (printer);
out:
  g_free (qualifier);
  g_strfreev (qualifiers);
}

static void
colord_client_device_connect_cb (GObject *source_object,
                                 GAsyncResult *res,
                                 gpointer user_data)
{
  GtkPrinterCpdb *printer = GTK_PRINTER_CPDB (user_data);
  gboolean ret;
  GError *error = NULL;

  /* get details about the device */
  ret = cd_device_connect_finish (CD_DEVICE (source_object), res, &error);
  if (!ret)
    {
      g_warning ("failed to get properties from the colord device: %s",
                 error->message);
      g_error_free (error);
      goto out;
    }
out:
  /* update the UI */
  colord_update_ui_from_settings (printer);

  g_object_unref (printer);
}

static void
colord_client_find_device_cb (GObject *source_object,
                              GAsyncResult *res,
                              gpointer user_data)
{
  GtkPrinterCpdb *printer = GTK_PRINTER_CPDB (user_data);
  GError *error = NULL;

  /* get the new device */
  printer->colord_device = cd_client_find_device_finish (printer->colord_client,
                                                         res,
                                                         &error);
  if (printer->colord_device == NULL)
    {
      g_warning ("failed to get find a colord device: %s",
                 error->message);
      g_error_free (error);
      goto out;
    }

  /* get details about the device */
  g_cancellable_reset (printer->colord_cancellable);
  cd_device_connect (printer->colord_device,
                     printer->colord_cancellable,
                     colord_client_device_connect_cb,
                     g_object_ref (printer));
out:
  /* update the UI */
  colord_update_ui_from_settings (printer);

  g_object_unref (printer);
}

static void
colord_update_device (GtkPrinterCpdb *printer)
{
  char *colord_device_id = NULL;

  /* not yet connected to the daemon */
  if (!cd_client_get_connected (printer->colord_client))
    goto out;

  /* not yet assigned a printer */
  if (printer->ppd_file == NULL)
    goto out;

  /* old cached profile no longer valid */
  if (printer->colord_profile)
    {
      g_object_unref (printer->colord_profile);
      printer->colord_profile = NULL;
    }

  /* old cached device no longer valid */
  if (printer->colord_device)
    {
      g_object_unref (printer->colord_device);
      printer->colord_device = NULL;
    }

  /* generate a known ID */
  colord_device_id = g_strdup_printf ("cpdb-%s", gtk_printer_get_name (GTK_PRINTER (printer)));

  g_cancellable_reset (printer->colord_cancellable);
  cd_client_find_device (printer->colord_client,
                         colord_device_id,
                         printer->colord_cancellable,
                         colord_client_find_device_cb,
                         g_object_ref (printer));
out:
  g_free (colord_device_id);

  /* update the UI */
  colord_update_ui_from_settings (printer);
}

static void
colord_client_connect_cb (GObject *source_object,
                          GAsyncResult *res,
                          gpointer user_data)
{
  gboolean ret;
  GError *error = NULL;
  GtkPrinterCpdb *printer = GTK_PRINTER_CPDB (user_data);
  static gboolean colord_warned = FALSE;

  ret = cd_client_connect_finish (CD_CLIENT (source_object),
                                  res, &error);
  if (!ret)
    {
      if (!colord_warned)
        {
          g_warning ("failed to contact colord: %s", error->message);
          colord_warned = TRUE;
        }
      g_error_free (error);
    }

  /* refresh the device */
  colord_update_device (printer);

  g_object_unref (printer);
}

static void
colord_printer_details_aquired_cb (GtkPrinterCpdb *printer,
                                   gboolean success,
                                   gpointer user_data)
{
  /* refresh the device */
  if (printer->colord_client)
    colord_update_device (printer);
}
#endif

/**
 * gtk_printer_cpdb_new:
 *
 * Creates a new #GtkPrinterCpdb.
 *
 * Returns: a new #GtkPrinterCpdb
 **/
GtkPrinterCpdb *
gtk_printer_cpdb_new (const char *name,
                      GtkPrintBackend *backend,
                      gpointer colord_client)
{
  GObject *result;
  gboolean accepts_pdf;
  GtkPrinterCpdb *printer;

#if (CPDB_VERSION_MAJOR == 1 && CPDB_VERSION_MINOR >= 2) || CPDB_VERSION_MAJOR > 1
  accepts_pdf = TRUE;
#else
  accepts_pdf = FALSE;
#endif

  result = g_object_new (GTK_TYPE_PRINTER_CPDB,
                         "name", name,
                         "backend", backend,
                         "is-virtual", FALSE,
                         "accepts-pdf", accepts_pdf,
                         NULL);
  printer = GTK_PRINTER_CPDB (result);

#ifdef HAVE_COLORD
  /* connect to colord */
  if (colord_client != NULL)
    {
      printer->colord_cancellable = g_cancellable_new ();
      printer->colord_client = g_object_ref (CD_CLIENT (colord_client));
      cd_client_connect (printer->colord_client,
                         printer->colord_cancellable,
                         colord_client_connect_cb,
                         g_object_ref (printer));
    }

  /* update the device when we read the PPD */
  g_signal_connect (printer, "details-acquired",
                    G_CALLBACK (colord_printer_details_aquired_cb),
                    printer);
#endif

  /*
   * IPP version 1.1 has to be supported
   * by all implementations according to rfc 2911
   */
  printer->ipp_version_major = 1;
  printer->ipp_version_minor = 1;

  return printer;
}

ppd_file_t *
gtk_printer_cpdb_get_ppd (GtkPrinterCpdb *printer)
{
  return printer->ppd_file;
}

const char *
gtk_printer_cpdb_get_ppd_name (GtkPrinterCpdb *printer)
{
  const char *result;

  result = printer->ppd_name;

  if (result == NULL)
    result = gtk_printer_get_name (GTK_PRINTER (printer));

  return result;
}
