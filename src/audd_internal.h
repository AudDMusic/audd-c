/* audd_internal.h — private types and helpers shared across the .c files. */
#ifndef AUDD_INTERNAL_H
#define AUDD_INTERNAL_H

#include "audd.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#include "../vendor/cJSON/cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ *
 * Memory                                                              *
 * ------------------------------------------------------------------ */

void  *audd_malloc(size_t size);
void   audd_free(void *ptr);
void  *audd_realloc(void *ptr, size_t size);
/* Free a buffer obtained from audd_realloc. Buffers grown through
 * audd_realloc must be released with this (not audd_free): under a custom
 * malloc-only allocator, audd_realloc carries a size header that only the
 * realloc path knows how to unwind. NULL-safe. */
void   audd_realloc_free(void *ptr);
char  *audd_strdup(const char *s);
char  *audd_strndup(const char *s, size_t n);

/* sprintf-like; returns heap string. NULL on OOM. */
char *audd_aprintf(const char *fmt, ...);

/* ------------------------------------------------------------------ *
 * String slice (for raw JSON capture, etc).                           *
 * ------------------------------------------------------------------ */

typedef struct {
    char  *data; /* heap, NUL-terminated, owns memory */
    size_t len;
} audd_str_t;

void audd_str_clear(audd_str_t *s);
int  audd_str_set(audd_str_t *s, const char *src, size_t len); /* 0 ok, -1 oom */

/* ------------------------------------------------------------------ *
 * HTTP                                                                *
 * ------------------------------------------------------------------ */

/* One file part for multipart upload. */
typedef struct {
    const char *name;          /* form field name (e.g. "file") */
    const char *filename;      /* basename for content-disposition */
    const char *content_type;  /* defaults application/octet-stream */
    const char *path;          /* if non-NULL, libcurl reads from disk */
    const void *data;          /* if path is NULL, sent as in-memory bytes */
    size_t      size;          /* size of `data` */
} audd_http_file_t;

/* Form fields and (optional) one file upload. */
typedef struct {
    /* NULL-terminated array of {key,value} pairs. */
    const char       **fields;  /* may be NULL */
    audd_http_file_t  *file;    /* may be NULL */
} audd_http_form_t;

typedef struct {
    long  status;        /* HTTP status code */
    char *body;          /* heap buffer (may be NULL when empty) */
    size_t body_len;
    char *request_id;    /* X-Request-Id (may be NULL) */
    cJSON *json;         /* parsed body, if any. NULL if non-JSON. */
} audd_http_response_t;

void audd_http_response_free(audd_http_response_t *r);

typedef enum {
    AUDD_RETRY_READ,         /* idempotent reads: 408/429/5xx + connection errs */
    AUDD_RETRY_RECOGNITION,  /* metered: 5xx + pre-upload connection errs only */
    AUDD_RETRY_MUTATING,     /* mutating: pre-upload connection errs only */
    AUDD_RETRY_NONE          /* never retry: exactly one attempt regardless of outcome */
} audd_retry_class_t;

/*
 * audd_failure_is_pre_upload classifies a failed transfer: returns 1 only
 * when the failure is known to have happened before any byte of the request
 * body was sent, so retrying cannot re-submit work the server may have
 * already performed (and billed).
 *
 *   curl_code           — the CURLcode from curl_easy_perform (as int)
 *   uploaded_body_bytes — CURLINFO_SIZE_UPLOAD_T probe of the easy handle
 *   http_status         — CURLINFO_RESPONSE_CODE probe (0 = no status line)
 *
 * Codes that can only occur before the transfer starts (DNS, connect, TLS
 * handshake, malformed URL) are pre-upload regardless of the probes.
 * Ambiguous codes (timeouts, send/recv errors, aborted transfers) are
 * pre-upload only if zero body bytes were handed to the transport AND no
 * HTTP status line came back. Exposed (rather than kept file-local) so the
 * test suite can verify the classification table directly.
 */
int audd_failure_is_pre_upload(int curl_code,
                               long long uploaded_body_bytes,
                               long http_status);

/*
 * Perform a POST with optional multipart form upload. `body_was_uploaded`
 * is set to 0 only when the failure provably happened before any request-
 * body byte was sent (see audd_failure_is_pre_upload); otherwise 1 — the
 * server may have received the request, so a metered retry is not safe.
 */
int audd_http_post(audd_client_t *client,
                   const char *url,
                   const audd_http_form_t *form,
                   long timeout_seconds,
                   audd_http_response_t *resp,
                   int *body_was_uploaded);

int audd_http_get(audd_client_t *client,
                  const char *url,
                  const char **query_kv,
                  long timeout_seconds,
                  audd_http_response_t *resp);

/*
 * Retry-driven request runner.
 *
 *   retries on the relevant set of failures based on `class`. The closure
 *   `do_one(client, ud)` performs a single HTTP attempt.
 */
typedef int (*audd_attempt_fn)(audd_client_t *client,
                               audd_http_response_t *resp,
                               int *body_was_uploaded,
                               void *ud);

int audd_retry_do(audd_client_t *client,
                  audd_retry_class_t class,
                  audd_attempt_fn attempt,
                  void *ud,
                  audd_http_response_t *resp);

/* ------------------------------------------------------------------ *
 * Client internals                                                    *
 * ------------------------------------------------------------------ */

struct audd_client {
    char *api_token;             /* heap, may be NULL — guarded by `lock` */
    audd_options_t options;      /* immutable after create */
    char *user_agent;            /* heap — immutable after create */
    char *ca_bundle_path;        /* heap or NULL — immutable after create */

    /* last-error scratch state — guarded by `lock` */
    char *last_error_message;    /* heap or NULL */
    int   last_error_code;       /* AudD numeric */
    char *last_request_id;       /* heap or NULL */

    /* Guards api_token and the last-error/last-request-id fields so that
     * concurrent requests, token rotation, and getters on one client are
     * data-race free. */
    pthread_mutex_t lock;

    /* longpoll cancellation: shared "cancel" flag bumped from any thread.
     * NOT a mutex-protected field — single producer, single consumer
     * monotonic increment. Sufficient for cancellation signaling. */
    volatile int closed;          /* client closed */
};

void audd_client_set_error(audd_client_t *client,
                           const char *message,
                           int api_code);
void audd_client_set_request_id(audd_client_t *client, const char *rid);
void audd_client_clear_error(audd_client_t *client);

/* Copy the current API token under the client lock. Returns a heap string
 * the caller frees with audd_free, or NULL if no token is set (or OOM).
 * Callers building a request snapshot the token this way so a concurrent
 * audd_client_set_api_token can't free it mid-flight. */
char *audd_client_copy_api_token(audd_client_t *client);

/* Map AudD numeric error code to sentinel. */
audd_error_t audd_sentinel_for_code(int code);

/* ------------------------------------------------------------------ *
 * JSON helpers                                                        *
 * ------------------------------------------------------------------ */

/* Lookup helpers: return NULL if not present or wrong type. */
const char *audd_json_get_string(const cJSON *obj, const char *key);
int         audd_json_get_int(const cJSON *obj, const char *key, int def);
int64_t     audd_json_get_int64(const cJSON *obj, const char *key, int64_t def);
double      audd_json_get_double(const cJSON *obj, const char *key, double def);
int         audd_json_has(const cJSON *obj, const char *key);
int         audd_json_get_bool(const cJSON *obj, const char *key, int def);

/* Get the printed JSON of `obj[key]` as a heap string ("string-quoted",
 * number, true/false, null, object, array). NULL if absent or OOM. */
char *audd_json_print_field(const cJSON *obj, const char *key);

/* Inspect "status" / "error" / "result" of a top-level response and return
 * an audd_error_t. On error, sets the client's last-error state. On 51 +
 * usable result, rewrites body to look like a success and returns AUDD_OK
 * (deprecation pass-through). */
audd_error_t audd_decode_or_raise(audd_client_t *client,
                                  audd_http_response_t *resp,
                                  int custom_catalog_context);

/* Top-level decode that doesn't strip — used by raw_request. */
audd_error_t audd_decode_top_level(audd_client_t *client,
                                   audd_http_response_t *resp);

/* ------------------------------------------------------------------ *
 * Recognition                                                          *
 * ------------------------------------------------------------------ */

audd_recognition_t *audd_recognition_from_json(const cJSON *result_obj);

audd_enterprise_result_t *audd_enterprise_from_json(const cJSON *result_arr);

/* Request-parameter assembly (shared with tests). */
typedef struct {
    const char *url;          /* URL source (NULL if not URL) */
    const char *file_path;    /* file path source (NULL if not path) */
    const void *bytes;        /* in-memory bytes (NULL if not bytes) */
    size_t      bytes_size;
    const char *return_csv;   /* heap or NULL */
    const char *market;
    const char **extra_parameters; /* NULL-terminated key,value,...,NULL */
    const audd_enterprise_options_t *eopts;
    int         is_enterprise;
} audd_recognize_ctx_t;

/* Scratch storage for the numeric typed fields, owned by the caller so the
 * pointers written into the field array stay valid. */
typedef struct {
    char skip[16];
    char every[16];
    char limit[16];
    char skip_first_seconds[16];
} audd_recognize_scratch_t;

/*
 * Assemble the alternating key/value field list for a recognize request into
 * `fields` (which must hold at least audd_recognize_fields_capacity(ctx)
 * entries). extra_parameters are emitted first, but any extras entry whose key
 * collides with a typed field that is also being sent is skipped, so typed
 * params win and no key is sent twice. Writes a trailing NULL and returns the
 * number of string slots used (excluding the terminator).
 */
size_t audd_build_recognize_fields(const audd_recognize_ctx_t *ctx,
                                   audd_recognize_scratch_t *scratch,
                                   const char **fields,
                                   size_t capacity);

/* Upper bound on the slot count needed by audd_build_recognize_fields
 * (including the NULL terminator). */
size_t audd_recognize_fields_capacity(const audd_recognize_ctx_t *ctx);

/* ------------------------------------------------------------------ *
 * Stream callback parsing                                              *
 * ------------------------------------------------------------------ */

audd_error_t audd_parse_callback_internal(const char *body,
                                          size_t size,
                                          audd_stream_callback_match_t **out_match,
                                          audd_stream_callback_notification_t **out_notification,
                                          char **out_error);

/* ------------------------------------------------------------------ *
 * Longpoll classification helpers (shared with tests).                *
 * ------------------------------------------------------------------ */

typedef enum {
    AUDD_LONGPOLL_CONTINUE,   /* consumed / keepalive / unrecognized — poll again */
    AUDD_LONGPOLL_RETRY,      /* transient failure — back off and reconnect */
    AUDD_LONGPOLL_TERMINAL    /* auth / API / fatal — stop the loop */
} audd_longpoll_action_t;

/* Classify a poll outcome. `rc` is the audd_http_get return (0 = got HTTP
 * response, non-zero = connection/timeout-class failure). `status` is the HTTP
 * status when rc == 0. Connection failures and transient HTTP statuses
 * (408/429/5xx) are RETRY; other >= 400 statuses are TERMINAL; otherwise the
 * caller inspects the body. */
audd_longpoll_action_t audd_longpoll_classify_http(int rc, long status);

/* Poll request timeout: the socket must outlast the server-side longpoll hold,
 * so use max(standard_timeout_seconds, hold_timeout_seconds + margin). */
long audd_longpoll_poll_timeout(long standard_timeout_seconds,
                                int hold_timeout_seconds);

/* ------------------------------------------------------------------ *
 * URL helpers                                                          *
 * ------------------------------------------------------------------ */

/* Append "?return=<csv>" (or "&return=…") to rawURL. Writes a heap string
 * to *out (caller frees with audd_free). Returns:
 *   AUDD_OK on success (out always non-NULL, even when csv is empty),
 *   AUDD_ERR_INVALID_ARGUMENT when rawURL already contains a `return=`. */
audd_error_t audd_url_append_return(const char *raw_url,
                                    const char **return_metadata,
                                    char **out);

/* Hostname check: does `url` parse with hostname == `host`? */
int audd_url_hostname_is(const char *url, const char *host);

int audd_url_is_http(const char *url);

/* ------------------------------------------------------------------ *
 * MD5 (for longpoll category derivation only; not crypto-grade).      *
 * ------------------------------------------------------------------ */

void audd_md5_hex(const void *data, size_t size, char out_hex[33]);

#ifdef __cplusplus
}
#endif

#endif /* AUDD_INTERNAL_H */
