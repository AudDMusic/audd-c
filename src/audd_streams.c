/* audd_streams.c — stream-management methods + longpoll category derivation. */
#include "audd_internal.h"
#include "audd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../vendor/cJSON/cJSON.h"

static const char *kApiBase = "https://api.audd.io";

struct audd_stream {
    int   radio_id;
    char *url;
    int   stream_running;
    char *longpoll_category;
};

struct audd_stream_list {
    audd_stream_t *items;
    size_t count;
};

/* ---------------- common: POST a stream-management request ---------------- */

typedef struct {
    const char *path;          /* e.g. "/setCallbackUrl/" */
    const char **fields;       /* NULL-terminated alternating key/value */
} streams_post_ctx_t;

static int streams_attempt(audd_client_t *client,
                            audd_http_response_t *resp,
                            int *body_was_uploaded,
                            void *ud)
{
    streams_post_ctx_t *ctx = (streams_post_ctx_t *)ud;
    char *url = audd_aprintf("%s%s", kApiBase, ctx->path);
    if (url == NULL) return -1;
    audd_http_form_t form = {0};
    form.fields = ctx->fields;
    int rc = audd_http_post(client, url, &form,
                             client->options.standard_timeout_seconds,
                             resp, body_was_uploaded);
    audd_free(url);
    return rc;
}

static audd_error_t streams_post(audd_client_t *client,
                                  const char *path,
                                  const char **fields,
                                  audd_retry_class_t class,
                                  cJSON **out_result)
{
    if (out_result) *out_result = NULL;
    streams_post_ctx_t ctx = { path, fields };
    audd_http_response_t resp = {0};
    int rc = audd_retry_do(client, class, streams_attempt, &ctx, &resp);
    if (rc != 0) {
        audd_http_response_free(&resp);
        return AUDD_ERR_CONNECTION;
    }
    audd_error_t derr = audd_decode_or_raise(client, &resp, 0);
    if (derr != AUDD_OK) {
        audd_http_response_free(&resp);
        return derr;
    }
    if (out_result) {
        cJSON *result = cJSON_GetObjectItemCaseSensitive(resp.json, "result");
        *out_result = result ? cJSON_Duplicate(result, 1) : NULL;
    }
    audd_http_response_free(&resp);
    return AUDD_OK;
}

/* ---------------- set callback url ---------------- */

audd_error_t audd_streams_set_callback_url(audd_client_t *client,
                                             const char *url,
                                             const audd_streams_set_callback_url_options_t *options)
{
    if (client == NULL || url == NULL) return AUDD_ERR_INVALID_ARGUMENT;
    char *final_url = NULL;
    audd_error_t e = audd_url_append_return(url,
        options ? options->return_metadata : NULL, &final_url);
    if (e != AUDD_OK) {
        if (e == AUDD_ERR_INVALID_ARGUMENT) {
            audd_client_set_error(client,
                "audd: callback URL already contains a `return` query parameter; "
                "pass return_metadata=NULL or remove the parameter from the URL", 0);
        }
        return e;
    }
    const char *fields[] = { "url", final_url, NULL };
    e = streams_post(client, "/setCallbackUrl/", fields, AUDD_RETRY_MUTATING, NULL);
    audd_free(final_url);
    return e;
}

audd_error_t audd_streams_get_callback_url(audd_client_t *client, char **out_url)
{
    if (client == NULL || out_url == NULL) return AUDD_ERR_INVALID_ARGUMENT;
    *out_url = NULL;
    cJSON *result = NULL;
    const char *fields[] = { NULL };
    audd_error_t e = streams_post(client, "/getCallbackUrl/", fields, AUDD_RETRY_READ, &result);
    if (e != AUDD_OK) return e;
    if (result != NULL && cJSON_IsString(result) && result->valuestring != NULL) {
        *out_url = audd_strdup(result->valuestring);
    }
    if (result) cJSON_Delete(result);
    return AUDD_OK;
}

audd_error_t audd_streams_add(audd_client_t *client, const audd_add_stream_request_t *req)
{
    if (client == NULL || req == NULL || req->url == NULL) return AUDD_ERR_INVALID_ARGUMENT;
    char rid_buf[16];
    snprintf(rid_buf, sizeof(rid_buf), "%d", req->radio_id);
    const char *fields[8];
    size_t f = 0;
    fields[f++] = "url";       fields[f++] = req->url;
    fields[f++] = "radio_id";  fields[f++] = rid_buf;
    if (req->callbacks && req->callbacks[0]) {
        fields[f++] = "callbacks"; fields[f++] = req->callbacks;
    }
    fields[f] = NULL;
    return streams_post(client, "/addStream/", fields, AUDD_RETRY_MUTATING, NULL);
}

audd_error_t audd_streams_set_url(audd_client_t *client, int radio_id, const char *url)
{
    if (client == NULL || url == NULL) return AUDD_ERR_INVALID_ARGUMENT;
    char rid_buf[16]; snprintf(rid_buf, sizeof(rid_buf), "%d", radio_id);
    const char *fields[] = { "radio_id", rid_buf, "url", url, NULL };
    return streams_post(client, "/setStreamUrl/", fields, AUDD_RETRY_MUTATING, NULL);
}

audd_error_t audd_streams_delete(audd_client_t *client, int radio_id)
{
    if (client == NULL) return AUDD_ERR_INVALID_ARGUMENT;
    char rid_buf[16]; snprintf(rid_buf, sizeof(rid_buf), "%d", radio_id);
    const char *fields[] = { "radio_id", rid_buf, NULL };
    return streams_post(client, "/deleteStream/", fields, AUDD_RETRY_MUTATING, NULL);
}

audd_error_t audd_streams_list(audd_client_t *client, audd_stream_list_t **out_list)
{
    if (client == NULL || out_list == NULL) return AUDD_ERR_INVALID_ARGUMENT;
    *out_list = NULL;
    cJSON *result = NULL;
    const char *fields[] = { NULL };
    audd_error_t e = streams_post(client, "/getStreams/", fields, AUDD_RETRY_READ, &result);
    if (e != AUDD_OK) return e;
    audd_stream_list_t *list = (audd_stream_list_t *)audd_malloc(sizeof(*list));
    if (list == NULL) {
        if (result) cJSON_Delete(result);
        return AUDD_ERR_OUT_OF_MEMORY;
    }
    memset(list, 0, sizeof(*list));
    if (result != NULL && cJSON_IsArray(result)) {
        int n = cJSON_GetArraySize(result);
        if (n > 0) {
            list->items = (audd_stream_t *)audd_malloc((size_t)n * sizeof(*list->items));
            if (list->items == NULL) {
                audd_free(list);
                if (result) cJSON_Delete(result);
                return AUDD_ERR_OUT_OF_MEMORY;
            }
            memset(list->items, 0, (size_t)n * sizeof(*list->items));
            for (int i = 0; i < n; ++i) {
                cJSON *it = cJSON_GetArrayItem(result, i);
                if (!cJSON_IsObject(it)) continue;
                audd_stream_t *s = &list->items[list->count];
                s->radio_id = audd_json_get_int(it, "radio_id", 0);
                const char *u = audd_json_get_string(it, "url");
                s->url = u ? audd_strdup(u) : NULL;
                s->stream_running = audd_json_get_bool(it, "stream_running", 0);
                const char *lc = audd_json_get_string(it, "longpoll_category");
                s->longpoll_category = lc ? audd_strdup(lc) : NULL;
                list->count++;
            }
        }
    }
    if (result) cJSON_Delete(result);
    *out_list = list;
    return AUDD_OK;
}

void audd_stream_list_free(audd_stream_list_t *list)
{
    if (list == NULL) return;
    for (size_t i = 0; i < list->count; ++i) {
        audd_free(list->items[i].url);
        audd_free(list->items[i].longpoll_category);
    }
    audd_free(list->items);
    audd_free(list);
}

size_t audd_stream_list_count(const audd_stream_list_t *list)
{
    return list ? list->count : 0;
}

const audd_stream_t *audd_stream_list_at(const audd_stream_list_t *list, size_t i)
{
    if (list == NULL || i >= list->count) return NULL;
    return &list->items[i];
}

int audd_stream_get_radio_id(const audd_stream_t *s) { return s ? s->radio_id : 0; }
const char *audd_stream_get_url(const audd_stream_t *s) { return s ? s->url : NULL; }
int audd_stream_get_running(const audd_stream_t *s) { return s ? s->stream_running : 0; }
const char *audd_stream_get_longpoll_category(const audd_stream_t *s) { return s ? s->longpoll_category : NULL; }

/* ---------------- longpoll category derivation ---------------- */

audd_error_t audd_derive_longpoll_category(const char *api_token, int radio_id, char *out_category)
{
    if (api_token == NULL || out_category == NULL) return AUDD_ERR_INVALID_ARGUMENT;
    char inner[33];
    audd_md5_hex(api_token, strlen(api_token), inner);
    char concat[64];
    int n = snprintf(concat, sizeof(concat), "%s%d", inner, radio_id);
    if (n < 0 || (size_t)n >= sizeof(concat)) return AUDD_ERR_INVALID_ARGUMENT;
    char outer[33];
    audd_md5_hex(concat, (size_t)n, outer);
    memcpy(out_category, outer, 9);
    out_category[9] = '\0';
    return AUDD_OK;
}

audd_error_t audd_streams_derive_longpoll_category(const audd_client_t *client,
                                                     int radio_id,
                                                     char *out_category)
{
    if (client == NULL) return AUDD_ERR_INVALID_ARGUMENT;
    if (client->api_token == NULL) return AUDD_ERR_AUTHENTICATION;
    return audd_derive_longpoll_category(client->api_token, radio_id, out_category);
}

/* Reference kApiBase to silence unused warning. */
__attribute__((unused)) static const char *_kab = NULL;
__attribute__((unused)) static void _ref(void) { _kab = kApiBase; }
