/* audd_internal.h — private types and helpers shared across the .c files. */
#ifndef AUDD_INTERNAL_H
#define AUDD_INTERNAL_H

#include "audd.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

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
    AUDD_RETRY_MUTATING      /* mutating: pre-upload connection errs only */
} audd_retry_class_t;

/*
 * Perform a POST with optional multipart form upload. `body_was_uploaded`
 * is set to 1 if the request reached the server (body uploaded); 0 if it
 * failed pre-upload (DNS / connect / TLS).
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
    char *api_token;             /* heap, may be NULL */
    audd_options_t options;
    char *user_agent;            /* heap */
    char *ca_bundle_path;        /* heap or NULL */

    /* last-error scratch state */
    char *last_error_message;    /* heap or NULL */
    int   last_error_code;       /* AudD numeric */
    char *last_request_id;       /* heap or NULL */

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

/* Map AudD numeric error code to sentinel. */
audd_error_t audd_sentinel_for_code(int code);

/* ------------------------------------------------------------------ *
 * JSON helpers                                                        *
 * ------------------------------------------------------------------ */

/* Lookup helpers: return NULL if not present or wrong type. */
const char *audd_json_get_string(const cJSON *obj, const char *key);
int         audd_json_get_int(const cJSON *obj, const char *key, int def);
int64_t     audd_json_get_int64(const cJSON *obj, const char *key, int64_t def);
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

/* ------------------------------------------------------------------ *
 * Stream callback parsing                                              *
 * ------------------------------------------------------------------ */

audd_error_t audd_parse_callback_internal(const char *body,
                                          size_t size,
                                          audd_stream_callback_match_t **out_match,
                                          audd_stream_callback_notification_t **out_notification,
                                          char **out_error);

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
