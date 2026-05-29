/* audd_http.c — libcurl-backed HTTP transport. */
#include "audd_internal.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include "../vendor/cJSON/cJSON.h"

/* ------------------------------------------------------------------ */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} buf_t;

static int buf_append(buf_t *b, const void *data, size_t n)
{
    size_t needed = b->len + n + 1;
    if (needed > b->cap) {
        size_t newcap = b->cap == 0 ? 4096 : b->cap;
        while (newcap < needed) newcap *= 2;
        char *p = (char *)audd_realloc(b->data, newcap);
        if (p == NULL) return -1;
        b->data = p;
        b->cap = newcap;
    }
    memcpy(b->data + b->len, data, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 0;
}

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t total = size * nmemb;
    buf_t *b = (buf_t *)userdata;
    if (buf_append(b, ptr, total) != 0) {
        return 0; /* libcurl treats short write as error */
    }
    return total;
}

static size_t header_cb(char *buffer, size_t size, size_t nitems, void *userdata)
{
    size_t total = size * nitems;
    char **out_request_id = (char **)userdata;
    /* Look for "X-Request-Id: ..." (case-insensitive). */
    static const char k[] = "X-Request-Id:";
    const size_t klen = sizeof(k) - 1;
    if (total > klen && strncasecmp(buffer, k, klen) == 0) {
        size_t start = klen;
        while (start < total && (buffer[start] == ' ' || buffer[start] == '\t')) ++start;
        size_t end = total;
        while (end > start && (buffer[end-1] == '\r' || buffer[end-1] == '\n' ||
                                buffer[end-1] == ' ' || buffer[end-1] == '\t')) --end;
        if (end > start) {
            char *rid = audd_strndup(buffer + start, end - start);
            if (rid != NULL) {
                audd_free(*out_request_id);
                *out_request_id = rid;
            }
        }
    }
    return total;
}

void audd_http_response_free(audd_http_response_t *r)
{
    if (r == NULL) return;
    audd_free(r->body);
    audd_free(r->request_id);
    if (r->json) cJSON_Delete(r->json);
    memset(r, 0, sizeof(*r));
}

/* Try to parse `body` as JSON; sets resp->json (may be NULL). */
static void try_parse_json(audd_http_response_t *resp)
{
    if (resp->body == NULL || resp->body_len == 0) return;
    resp->json = cJSON_ParseWithLength(resp->body, resp->body_len);
}

/* Build the User-Agent string for a client. */
static const char *user_agent_for(audd_client_t *client)
{
    return client->user_agent != NULL ? client->user_agent : "audd-c";
}

/* Configure common options on a curl handle. */
static void apply_common_opts(audd_client_t *client, CURL *eh, long timeout_seconds,
                              buf_t *body, char **request_id_out)
{
    curl_easy_setopt(eh, CURLOPT_USERAGENT, user_agent_for(client));
    curl_easy_setopt(eh, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(eh, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(eh, CURLOPT_TIMEOUT, (long)timeout_seconds);
    curl_easy_setopt(eh, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(eh, CURLOPT_WRITEDATA, body);
    curl_easy_setopt(eh, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(eh, CURLOPT_HEADERDATA, request_id_out);
    if (client->ca_bundle_path != NULL) {
        curl_easy_setopt(eh, CURLOPT_CAINFO, client->ca_bundle_path);
    }
    curl_easy_setopt(eh, CURLOPT_TCP_KEEPALIVE, 1L);
}

/* Map curl error codes to "did the body get uploaded?" — used to gate
 * recognition retries. Pre-upload codes mean the request never reached
 * the server's app layer. */
static int curl_was_pre_upload(CURLcode rc)
{
    switch (rc) {
    case CURLE_COULDNT_RESOLVE_PROXY:
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_CONNECT:
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_OPERATION_TIMEDOUT: /* may also be post; conservative for retries */
        return 1;
    default:
        return 0;
    }
}

int audd_http_post(audd_client_t *client,
                   const char *url,
                   const audd_http_form_t *form,
                   long timeout_seconds,
                   audd_http_response_t *resp,
                   int *body_was_uploaded)
{
    if (client == NULL || url == NULL || resp == NULL) {
        return -1;
    }
    memset(resp, 0, sizeof(*resp));
    if (body_was_uploaded) *body_was_uploaded = 1; /* assume uploaded; reset on early curl error */

    CURL *eh = curl_easy_init();
    if (eh == NULL) {
        audd_client_set_error(client, "curl_easy_init failed", 0);
        return -1;
    }

    buf_t body = {0};
    char *request_id = NULL;
    apply_common_opts(client, eh, timeout_seconds, &body, &request_id);
    curl_easy_setopt(eh, CURLOPT_URL, url);
    curl_easy_setopt(eh, CURLOPT_POST, 1L);

    /* Always use multipart MIME — AudD endpoints accept it for both
     * recognize uploads and form-only requests (set callback url, etc). */
    curl_mime *mime = curl_mime_init(eh);

    /* Always include api_token field. */
    if (client->api_token != NULL) {
        curl_mimepart *part = curl_mime_addpart(mime);
        curl_mime_name(part, "api_token");
        curl_mime_data(part, client->api_token, CURL_ZERO_TERMINATED);
    }

    /* Form fields. */
    if (form != NULL && form->fields != NULL) {
        for (size_t i = 0; form->fields[i] != NULL && form->fields[i+1] != NULL; i += 2) {
            const char *k = form->fields[i];
            const char *v = form->fields[i+1];
            if (k == NULL || v == NULL) continue;
            curl_mimepart *part = curl_mime_addpart(mime);
            curl_mime_name(part, k);
            curl_mime_data(part, v, CURL_ZERO_TERMINATED);
        }
    }

    /* File part. */
    if (form != NULL && form->file != NULL) {
        curl_mimepart *part = curl_mime_addpart(mime);
        curl_mime_name(part, form->file->name ? form->file->name : "file");
        curl_mime_filename(part, form->file->filename ? form->file->filename : "upload.bin");
        if (form->file->content_type != NULL) {
            curl_mime_type(part, form->file->content_type);
        } else {
            curl_mime_type(part, "application/octet-stream");
        }
        if (form->file->path != NULL) {
            curl_mime_filedata(part, form->file->path);
        } else {
            curl_mime_data(part, (const char *)form->file->data, form->file->size);
        }
    }
    curl_easy_setopt(eh, CURLOPT_MIMEPOST, mime);

    CURLcode rc = curl_easy_perform(eh);
    long status = 0;
    curl_easy_getinfo(eh, CURLINFO_RESPONSE_CODE, &status);

    if (rc != CURLE_OK) {
        if (body_was_uploaded) *body_was_uploaded = curl_was_pre_upload(rc) ? 0 : 1;
        char *msg = audd_aprintf("audd: connection error: %s", curl_easy_strerror(rc));
        audd_client_set_error(client, msg ? msg : "connection error", 0);
        audd_free(msg);
        audd_free(body.data);
        audd_free(request_id);
        curl_mime_free(mime);
        curl_easy_cleanup(eh);
        return -1;
    }

    resp->status = status;
    resp->body = body.data;
    resp->body_len = body.len;
    resp->request_id = request_id;
    audd_client_set_request_id(client, request_id);
    try_parse_json(resp);

    curl_mime_free(mime);
    curl_easy_cleanup(eh);
    return 0;
}

int audd_http_get(audd_client_t *client,
                  const char *url,
                  const char **query_kv,
                  long timeout_seconds,
                  audd_http_response_t *resp)
{
    if (client == NULL || url == NULL || resp == NULL) return -1;
    memset(resp, 0, sizeof(*resp));

    /* Build URL with query string. */
    char *full_url = NULL;
    if (query_kv != NULL && query_kv[0] != NULL) {
        size_t total = strlen(url) + 1; /* '?' */
        for (size_t i = 0; query_kv[i] != NULL && query_kv[i+1] != NULL; i += 2) {
            total += strlen(query_kv[i]) * 3 + 1 + strlen(query_kv[i+1]) * 3 + 1;
        }
        full_url = (char *)audd_malloc(total + 16);
        if (full_url == NULL) {
            audd_client_set_error(client, "out of memory", 0);
            return -1;
        }
        size_t off = 0;
        memcpy(full_url + off, url, strlen(url));
        off += strlen(url);
        full_url[off++] = (strchr(url, '?') == NULL) ? '?' : '&';
        int first = 1;
        for (size_t i = 0; query_kv[i] != NULL && query_kv[i+1] != NULL; i += 2) {
            if (!first) full_url[off++] = '&';
            first = 0;
            CURL *tmp = curl_easy_init();
            char *kk = curl_easy_escape(tmp, query_kv[i], 0);
            char *vv = curl_easy_escape(tmp, query_kv[i+1], 0);
            size_t kl = strlen(kk), vl = strlen(vv);
            memcpy(full_url + off, kk, kl); off += kl;
            full_url[off++] = '=';
            memcpy(full_url + off, vv, vl); off += vl;
            curl_free(kk); curl_free(vv);
            curl_easy_cleanup(tmp);
        }
        full_url[off] = '\0';
    } else {
        full_url = audd_strdup(url);
        if (full_url == NULL) {
            audd_client_set_error(client, "out of memory", 0);
            return -1;
        }
    }

    CURL *eh = curl_easy_init();
    if (eh == NULL) {
        audd_free(full_url);
        audd_client_set_error(client, "curl_easy_init failed", 0);
        return -1;
    }
    buf_t body = {0};
    char *request_id = NULL;
    apply_common_opts(client, eh, timeout_seconds, &body, &request_id);
    curl_easy_setopt(eh, CURLOPT_URL, full_url);
    curl_easy_setopt(eh, CURLOPT_HTTPGET, 1L);

    CURLcode rc = curl_easy_perform(eh);
    long status = 0;
    curl_easy_getinfo(eh, CURLINFO_RESPONSE_CODE, &status);
    if (rc != CURLE_OK) {
        char *msg = audd_aprintf("audd: connection error: %s", curl_easy_strerror(rc));
        audd_client_set_error(client, msg ? msg : "connection error", 0);
        audd_free(msg);
        audd_free(body.data);
        audd_free(request_id);
        audd_free(full_url);
        curl_easy_cleanup(eh);
        return -1;
    }
    resp->status = status;
    resp->body = body.data;
    resp->body_len = body.len;
    resp->request_id = request_id;
    audd_client_set_request_id(client, request_id);
    try_parse_json(resp);

    audd_free(full_url);
    curl_easy_cleanup(eh);
    return 0;
}

/* ------------------------------------------------------------------ *
 * Retry-driven runner.                                                 *
 * ------------------------------------------------------------------ */

#include <time.h>

static void sleep_ms(long ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/* Should we retry given (attempt outcome, class)?
 * Returns 1 if retry permitted, 0 otherwise. */
static int should_retry(audd_retry_class_t class, int rc, long status, int body_uploaded)
{
    if (rc == 0) {
        /* Got an HTTP response. */
        if (status < 400) return 0;
        switch (class) {
        case AUDD_RETRY_READ:
            return (status == 408 || status == 429 || status >= 500);
        case AUDD_RETRY_RECOGNITION:
            return (status >= 500);
        case AUDD_RETRY_MUTATING:
            return 0;
        case AUDD_RETRY_NONE:
            return 0;
        }
        return 0;
    }
    /* Connection-class error. */
    switch (class) {
    case AUDD_RETRY_READ:
        return 1;
    case AUDD_RETRY_RECOGNITION:
    case AUDD_RETRY_MUTATING:
        return body_uploaded == 0;
    case AUDD_RETRY_NONE:
        return 0;
    }
    return 0;
}

int audd_retry_do(audd_client_t *client,
                  audd_retry_class_t class,
                  audd_attempt_fn attempt,
                  void *ud,
                  audd_http_response_t *resp)
{
    int max_attempts = client->options.max_attempts > 0 ? client->options.max_attempts : 3;
    long backoff_ms = client->options.backoff_ms > 0 ? client->options.backoff_ms : 500;

    for (int attempt_n = 1; attempt_n <= max_attempts; ++attempt_n) {
        if (client->closed) {
            audd_client_set_error(client, "client closed", 0);
            return -1;
        }
        audd_http_response_free(resp);
        int body_uploaded = 1;
        int rc = attempt(client, resp, &body_uploaded, ud);
        if (attempt_n == max_attempts) return rc;
        if (!should_retry(class, rc, resp->status, body_uploaded)) {
            return rc;
        }
        sleep_ms(backoff_ms * (long)(1 << (attempt_n - 1)));
    }
    return 0;
}
