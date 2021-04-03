
#ifndef __GTK_CPDB_UTILS_H__
#define __GTK_CPDB_UTILS_H__

#include <cups/cups.h>
#include <cups/http.h>
#include <cups/ipp.h>
#include <cups/language.h>
#include <glib.h>

G_BEGIN_DECLS

typedef struct _GtkCpdbRequest GtkCpdbRequest;

typedef struct _GtkCpdbConnectionTest GtkCpdbConnectionTest;

typedef enum
{
  GTK_CPDB_ERROR_HTTP,
  GTK_CPDB_ERROR_IPP,
  GTK_CPDB_ERROR_IO,
  GTK_CPDB_ERROR_AUTH,
  GTK_CPDB_ERROR_GENERAL
} GtkCpdbErrorType;

typedef enum
{
  GTK_CPDB_POST,
  GTK_CPDB_GET
} GtkCpdbRequestType;

/** 
 * Direction we should be polling the http socket on.
 * We are either reading or writing at each state.
 * This makes it easy for mainloops to connect to poll.
 */
typedef enum
{
  GTK_CPDB_HTTP_IDLE,
  GTK_CPDB_HTTP_READ,
  GTK_CPDB_HTTP_WRITE
} GtkCpdbPollState;

typedef enum
{
  GTK_CPDB_CONNECTION_AVAILABLE,
  GTK_CPDB_CONNECTION_NOT_AVAILABLE,
  GTK_CPDB_CONNECTION_IN_PROGRESS
} GtkCpdbConnectionState;

typedef enum
{
  GTK_CPDB_PASSWORD_NONE,
  GTK_CPDB_PASSWORD_REQUESTED,
  GTK_CPDB_PASSWORD_HAS,
  GTK_CPDB_PASSWORD_APPLIED,
  GTK_CPDB_PASSWORD_NOT_VALID
} GtkCpdbPasswordState;

typedef struct _GtkCpdbResult
{
  char *error_msg;
  ipp_t *ipp_response;
  GtkCpdbErrorType error_type;

  /* some error types like HTTP_ERROR have a status and a code */
  int error_status;
  int error_code;

  guint is_error : 1;
  guint is_ipp_response : 1;
} GtkCpdbResult;

typedef struct _GtkCpdbRequest
{
  GtkCpdbRequestType type;

  http_t *http;
  http_status_t last_status;
  ipp_t *ipp_request;

  char *server;
  char *resource;
  GIOChannel *data_io;
  int attempts;
  GtkCpdbResult *result;

  int state;
  GtkCpdbPollState poll_state;
  guint64 bytes_received;

  char *password;
  char *username;

  int own_http : 1;
  int need_password : 1;
  int need_auth_info : 1;
  char **auth_info_required;
  char **auth_info;
  GtkCpdbPasswordState password_state;

} GtkCpdbRequest;

typedef struct _GtkCpdbConnectionTest
{
  GtkCpdbConnectionState at_init;
  http_addrlist_t *addrlist;
  http_addrlist_t *current_addr;
  http_addrlist_t *last_wrong_addr;
  int socket;
} GtkCpdbConnectionTest;



#define GTK_CPDB_REQUEST_START 0
#define GTK_CPDB_REQUEST_DONE 500

/* POST states */
enum
{
  GTK_CPDB_POST_CONNECT = GTK_CPDB_REQUEST_START,
  GTK_CPDB_POST_SEND,
  GTK_CPDB_POST_WRITE_REQUEST,
  GTK_CPDB_POST_WRITE_DATA,
  GTK_CPDB_POST_CHECK,
  GTK_CPDB_POST_AUTH,
  GTK_CPDB_POST_READ_RESPONSE,
  GTK_CPDB_POST_DONE = GTK_CPDB_REQUEST_DONE
};

/* GET states */
enum
{
  GTK_CPDB_GET_CONNECT = GTK_CPDB_REQUEST_START,
  GTK_CPDB_GET_SEND,
  GTK_CPDB_GET_CHECK,
  GTK_CPDB_GET_AUTH,
  GTK_CPDB_GET_READ_DATA,
  GTK_CPDB_GET_DONE = GTK_CPDB_REQUEST_DONE
};

GtkCpdbRequest *gtk_cpdb_request_new_with_username (http_t *connection,
                                                    GtkCpdbRequestType req_type,
                                                    int operation_id,
                                                    GIOChannel *data_io,
                                                    const char *server,
                                                    const char *resource,
                                                    const char *username);
GtkCpdbRequest *gtk_cpdb_request_new (http_t *connection,
                                      GtkCpdbRequestType req_type,
                                      int operation_id,
                                      GIOChannel *data_io,
                                      const char *server,
                                      const char *resource);
void gtk_cpdb_request_ipp_add_string (GtkCpdbRequest *request,
                                      ipp_tag_t group,
                                      ipp_tag_t tag,
                                      const char *name,
                                      const char *charset,
                                      const char *value);
void gtk_cpdb_request_ipp_add_strings (GtkCpdbRequest *request,
                                       ipp_tag_t group,
                                       ipp_tag_t tag,
                                       const char *name,
                                       int num_values,
                                       const char *charset,
                                       const char *const *values);

gboolean gtk_cpdb_request_read_write (GtkCpdbRequest *request,
                                      gboolean connect_only);
GtkCpdbPollState gtk_cpdb_request_get_poll_state (GtkCpdbRequest *request);
void gtk_cpdb_request_free (GtkCpdbRequest *request);
GtkCpdbResult *gtk_cpdb_request_get_result (GtkCpdbRequest *request);
gboolean gtk_cpdb_request_is_done (GtkCpdbRequest *request);
void gtk_cpdb_request_encode_option (GtkCpdbRequest *request,
                                     const char *option,
                                     const char *value);
void gtk_cpdb_request_set_ipp_version (GtkCpdbRequest *request,
                                       int major,
                                       int minor);
gboolean gtk_cpdb_result_is_error (GtkCpdbResult *result);
ipp_t *gtk_cpdb_result_get_response (GtkCpdbResult *result);
GtkCpdbErrorType gtk_cpdb_result_get_error_type (GtkCpdbResult *result);
int gtk_cpdb_result_get_error_status (GtkCpdbResult *result);
int gtk_cpdb_result_get_error_code (GtkCpdbResult *result);
const char *gtk_cpdb_result_get_error_string (GtkCpdbResult *result);
GtkCpdbConnectionTest *gtk_cpdb_connection_test_new (const char *server,
                                                     const int port);
GtkCpdbConnectionState gtk_cpdb_connection_test_get_state (GtkCpdbConnectionTest *test);
void gtk_cpdb_connection_test_free (GtkCpdbConnectionTest *test);

G_END_DECLS
#endif
