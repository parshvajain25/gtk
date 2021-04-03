#ifndef __GTK_PRINTER_CPDB_H__
#define __GTK_PRINTER_CPDB_H__

#include "gtkcpdbutils.h"
#include <cups/cups.h>
#include <cups/ppd.h>
#include <glib-object.h>

#include <gtk/gtkprinterprivate.h>
#include <gtk/gtkunixprint.h>

#ifdef HAVE_COLORD
#include <colord.h>
#endif

G_BEGIN_DECLS

#define GTK_TYPE_PRINTER_CPDB (gtk_printer_cpdb_get_type ())
#define GTK_PRINTER_CPDB(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_TYPE_PRINTER_CPDB, GtkPrinterCpdb))
#define GTK_PRINTER_CPDB_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GTK_TYPE_PRINTER_CPDB, GtkPrinterCpdbClass))
#define GTK_IS_PRINTER_CPDB(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTK_TYPE_PRINTER_CPDB))
#define GTK_IS_PRINTER_CPDB_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GTK_TYPE_PRINTER_CPDB))
#define GTK_PRINTER_CPDB_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GTK_TYPE_PRINTER_CPDB, GtkPrinterCpdbClass))

typedef struct _GtkPrinterCpdb GtkPrinterCpdb;
typedef struct _GtkPrinterCpdbClass GtkPrinterCpdbClass;
typedef struct _GtkPrinterCpdbPrivate GtkPrinterCpdbPrivate;

struct _GtkPrinterCpdb
{
  GtkPrinter parent_instance;

  char *device_uri;
  char *original_device_uri;
  char *printer_uri;
  char *hostname;
  int port;
  char **auth_info_required;
  char *original_hostname;
  char *original_resource;
  int original_port;
  gboolean request_original_uri; /* Request PPD from original host */

  ipp_pstate_t state;
  gboolean reading_ppd;
  char *ppd_name;
  ppd_file_t *ppd_file;

  char *media_default;
  GList *media_supported;
  GList *media_size_supported;
  int media_bottom_margin_default;
  int media_top_margin_default;
  int media_left_margin_default;
  int media_right_margin_default;
  gboolean media_margin_default_set;
  char *sides_default;
  GList *sides_supported;
  char *output_bin_default;
  GList *output_bin_supported;

  char *default_cover_before;
  char *default_cover_after;

  int default_number_up;

  gboolean remote;
  guint get_remote_ppd_poll;
  int get_remote_ppd_attempts;
  GtkCpdbConnectionTest *remote_cpdb_connection_test;

#ifdef HAVE_COLORD
  CdClient *colord_client;
  CdDevice *colord_device;
  CdProfile *colord_profile;
  GCancellable *colord_cancellable;
  char *colord_title;
  char *colord_qualifier;
#endif

  gboolean avahi_browsed;
  char *avahi_name;
  char *avahi_type;
  char *avahi_domain;

  guchar ipp_version_major;
  guchar ipp_version_minor;
  gboolean supports_copies;
  gboolean supports_collate;
  gboolean supports_number_up;
  char **covers;
  int number_of_covers;
};

struct _GtkPrinterCpdbClass
{
  GtkPrinterClass parent_class;
};

static void gtk_printer_cpdb_init (GtkPrinterCpdb *printer);
static void gtk_printer_cpdb_class_init (GtkPrinterCpdbClass *class);
static void gtk_printer_cpdb_finalize (GObject *object);

static void gtk_printer_cpdb_set_property (GObject *object,
                                           guint prop_id,
                                           const GValue *value,
                                           GParamSpec *pspec);
static void gtk_printer_cpdb_get_property (GObject *object,
                                           guint prop_id,
                                           GValue *value,
                                           GParamSpec *pspec);

GType gtk_printer_cpdb_get_type (void) G_GNUC_CONST;
void gtk_printer_cpdb_register_type (GTypeModule *module);

GtkPrinterCpdb *gtk_printer_cpdb_new (const char *name,
                                      GtkPrintBackend *backend,
                                      gpointer colord_client);
ppd_file_t *gtk_printer_cpdb_get_ppd (GtkPrinterCpdb *printer);
const char *gtk_printer_cpdb_get_ppd_name (GtkPrinterCpdb *printer);

#ifdef HAVE_COLORD
void gtk_printer_cpdb_update_settings (GtkPrinterCpdb *printer,
                                       GtkPrintSettings *settings,
                                       GtkPrinterOptionSet *set);
#endif

G_END_DECLS

#endif /* __GTK_PRINTER_CPDB_H__ */
