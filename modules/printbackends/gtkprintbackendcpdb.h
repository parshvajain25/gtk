#ifndef __GTK_PRINT_BACKEND_CPDB_H__
#define __GTK_PRINT_BACKEND_CPDB_H__

#include "gtkprintbackendprivate.h"
#include "gtkprinter.h"
#include <cpdb-libs-frontend.h>
#include <glib-object.h>
#include <gtkprinter.h>
#include "gtkcpdbutils.h"
#include "gtkprintercpdb.h"

G_BEGIN_DECLS

#define GTK_TYPE_PRINTER_CPDB (gtk_printer_cpdb_get_type ())
#define GTK_TYPE_PRINT_BACKEND_CPDB (gtk_print_backend_cpdb_get_type ())
#define GTK_PRINT_BACKEND_CPDB(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_TYPE_PRINT_BACKEND_CPDB, GtkPrintBackendCpdb))
#define GTK_IS_PRINT_BACKEND_CPDB(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTK_TYPE_PRINT_BACKEND_CPDB))
#define GTK_PRINTER_CPDB(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_TYPE_PRINTER_CPDB, GtkPrinterCpdb))
#define GTK_PRINT_BACKEND_CPDB_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GTK_TYPE_PRINT_BACKEND_CPDB, GtkPrintBackendCpdbClass))
#define GTK_IS_PRINT_BACKEND_CPDB_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GTK_TYPE_PRINT_BACKEND_CPDB))
#define GTK_PRINT_BACKEND_CPDB_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GTK_TYPE_PRINT_BACKEND_CPDB, GtkPrintBackendCpdbClass))
#define GTK_CPDB_REQUEST_START 0
#define GTK_CPDB_REQUEST_DONE 500
typedef struct _GtkPrinterPrivate GtkPrinterPrivate;

typedef struct
{
  GtkPrintBackendClass parent_class;
} GtkPrintBackendCpdbClass;

typedef struct
{
  GtkPrintBackend parent_instance;
  guint list_printers_pending : 1;
  int list_printers_attempts;
  guint got_default_printer : 1;
  guint default_printer_poll;
  GtkCpdbConnectionTest *cpdb_connection_test;
  int reading_ppds;

  GList *requests;
  GHashTable *auth;
  char *username;
  gboolean authentication_lock;
  
#ifdef HAVE_COLORD
  CdClient *colord_client;
#endif

  GDBusConnection *dbus_connection;
  char *avahi_default_printer;
  guint avahi_service_browser_subscription_id;
  guint avahi_service_browser_subscription_ids[2];
  char *avahi_service_browser_paths[2];
  GCancellable *avahi_cancellable;

  gboolean secrets_service_available;
  guint secrets_service_watch_id;
  GCancellable *secrets_service_cancellable;
} GtkPrintBackendCpdb;

typedef struct
{
  GtkCpdbRequest *request;
  GtkPageSetup *page_setup;
  GtkPrinterCpdb *printer;
} CpdbOptionsData;



typedef void (*GtkPrintCpdbResponseCallbackFunc) (GtkPrintBackend *print_backend,
                                                  GtkCpdbResult *result,
                                                  gpointer user_data);

static GObjectClass *backend_parent_class;
static FrontendObj *frontendObj;
static GtkPrintBackend *gtkPrintBackend;

GtkPrintBackend *gtk_print_backend_cpdb_new (void);
GType gtk_print_backend_cpdb_get_type (void) G_GNUC_CONST;

static void add_cpdb_options (const char *key,
                              const char *value,
                              gpointer user_data);

static void cpdb_get_printer_list (GtkPrintBackend *backend);


static void
cpdb_dispatch_add_poll (GSource *source);

static void cpdb_printer_get_settings_from_options (GtkPrinter *printer,
                                                    GtkPrinterOptionSet *options,
                                                    GtkPrintSettings *settings);
static GtkPrinterOptionSet *cpdb_printer_get_options (GtkPrinter *printer,
                                                      GtkPrintSettings *settings,
                                                      GtkPageSetup *page_setup,
                                                      GtkPrintCapabilities capabilities);
static void cpdb_printer_prepare_for_print (GtkPrinter *printer,
                                            GtkPrintJob *print_job,
                                            GtkPrintSettings *settings,
                                            GtkPageSetup *page_setup);
static cairo_surface_t *cpdb_printer_create_cairo_surface (GtkPrinter *printer,
                                                           GtkPrintSettings *settings,
                                                           double width,
                                                           double height,
                                                           GIOChannel *cache_io);
static void gtk_print_backend_cpdb_print_stream (GtkPrintBackend *print_backend,
                                                 GtkPrintJob *job,
                                                 GIOChannel *data_io,
                                                 GtkPrintJobCompleteFunc callback,
                                                 gpointer user_data,
                                                 GDestroyNotify dnotify);


static int add_printer_callback (PrinterObj *p);

static int remove_printer_callback (PrinterObj *p);

static void cpdb_begin_polling_info (GtkPrintBackendCpdb *print_backend,
                                     GtkPrintJob *job,
                                     int job_id);

static gboolean
cpdb_job_info_poll_timeout (gpointer user_data);



G_END_DECLS

#endif