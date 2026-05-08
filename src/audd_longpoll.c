/* audd_longpoll.c — blocking longpoll consumer with callback dispatch. */
#include "audd_internal.h"
#include "audd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../vendor/cJSON/cJSON.h"

static const char *kApiBase = "https://api.audd.io";
static const int kNoCallbackErrorCode = 19;

struct audd_longpoll {
    volatile int closed;
};

audd_longpoll_options_t audd_longpoll_options_default(void)
{
    audd_longpoll_options_t o;
    o.since_time = 0;
    o.timeout = 50;
    o.skip_callback_check = 0;
    return o;
}

void audd_longpoll_close(audd_longpoll_t *handle)
{
    if (handle == NULL) return;
    handle->closed = 1;
}

/* Returns 1 if the body is the keepalive shape: {timeout: ...} with no
 * result/notification keys. */
static int is_keepalive(cJSON *body)
{
    if (body == NULL) return 0;
    if (cJSON_HasObjectItem(body, "result")) return 0;
    if (cJSON_HasObjectItem(body, "notification")) return 0;
    return cJSON_HasObjectItem(body, "timeout");
}

/* Surface a friendly hint when the account hasn't set a callback URL — the
 * silent-failure mode for longpoll. */
static audd_error_t preflight_callback_url(audd_client_t *client)
{
    char *url = NULL;
    audd_error_t e = audd_streams_get_callback_url(client, &url);
    audd_free(url);
    if (e == AUDD_OK) return AUDD_OK;
    if (client->last_error_code == kNoCallbackErrorCode) {
        audd_client_set_error(client,
            "Longpoll won't deliver events because no callback URL is "
            "configured for this account. Set one first via "
            "audd_streams_set_callback_url(client, url, opts) — "
            "\"https://audd.tech/empty/\" is fine if you only want "
            "longpolling and don't need a real receiver. To skip this "
            "check, set audd_longpoll_options_t.skip_callback_check = 1.",
            kNoCallbackErrorCode);
        return e;
    }
    return e;
}

static void emit_error(const audd_longpoll_callbacks_t *cb,
                       audd_error_t err, const char *msg)
{
    if (cb == NULL || cb->on_error == NULL) return;
    cb->on_error(err, msg ? msg : audd_error_string(err), cb->user_data);
}

audd_error_t audd_longpoll_run(audd_client_t *client,
                                const char *category,
                                const audd_longpoll_options_t *options,
                                const audd_longpoll_callbacks_t *callbacks,
                                audd_longpoll_t **out_handle)
{
    if (client == NULL || category == NULL || callbacks == NULL) {
        return AUDD_ERR_INVALID_ARGUMENT;
    }
    audd_longpoll_options_t o = options ? *options : audd_longpoll_options_default();
    if (o.timeout <= 0) o.timeout = 50;

    if (!o.skip_callback_check) {
        audd_error_t pe = preflight_callback_url(client);
        if (pe != AUDD_OK) {
            emit_error(callbacks, pe, audd_last_error_message(client));
            return pe;
        }
    }

    audd_longpoll_t *handle = (audd_longpoll_t *)audd_malloc(sizeof(*handle));
    if (handle == NULL) return AUDD_ERR_OUT_OF_MEMORY;
    handle->closed = 0;
    if (out_handle) *out_handle = handle;

    long since_time = o.since_time;
    char *url = audd_aprintf("%s/longpoll/", kApiBase);
    if (url == NULL) {
        audd_free(handle);
        if (out_handle) *out_handle = NULL;
        return AUDD_ERR_OUT_OF_MEMORY;
    }

    audd_error_t result_err = AUDD_OK;
    while (!handle->closed && !client->closed) {
        char timeout_buf[16];
        snprintf(timeout_buf, sizeof(timeout_buf), "%d", o.timeout);
        char since_buf[24];
        const char *kv[8];
        size_t k = 0;
        kv[k++] = "category"; kv[k++] = category;
        kv[k++] = "timeout";  kv[k++] = timeout_buf;
        if (since_time > 0) {
            snprintf(since_buf, sizeof(since_buf), "%ld", since_time);
            kv[k++] = "since_time"; kv[k++] = since_buf;
        }
        kv[k] = NULL;

        audd_http_response_t resp = {0};
        int rc = audd_http_get(client, url, kv,
                                client->options.standard_timeout_seconds,
                                &resp);
        if (rc != 0) {
            emit_error(callbacks, AUDD_ERR_CONNECTION, audd_last_error_message(client));
            result_err = AUDD_ERR_CONNECTION;
            audd_http_response_free(&resp);
            break;
        }
        if (resp.status >= 400) {
            char *m = audd_aprintf("Longpoll endpoint returned HTTP %ld", resp.status);
            audd_client_set_error(client, m ? m : "longpoll http error", 0);
            audd_free(m);
            emit_error(callbacks, AUDD_ERR_SERVER, audd_last_error_message(client));
            result_err = AUDD_ERR_SERVER;
            audd_http_response_free(&resp);
            break;
        }
        if (resp.body == NULL || resp.body_len == 0) {
            audd_client_set_error(client, "Longpoll response was empty", 0);
            emit_error(callbacks, AUDD_ERR_SERIALIZATION, audd_last_error_message(client));
            result_err = AUDD_ERR_SERIALIZATION;
            audd_http_response_free(&resp);
            break;
        }
        if (is_keepalive(resp.json)) {
            cJSON *ts = cJSON_GetObjectItemCaseSensitive(resp.json, "timestamp");
            if (cJSON_IsNumber(ts)) {
                since_time = (long)ts->valuedouble;
            }
            audd_http_response_free(&resp);
            continue;
        }

        audd_stream_callback_match_t *m = NULL;
        audd_stream_callback_notification_t *n = NULL;
        char *parse_err = NULL;
        audd_error_t pe = audd_parse_callback(resp.body, resp.body_len, &m, &n, &parse_err);
        if (pe != AUDD_OK) {
            audd_client_set_error(client, parse_err ? parse_err : "parse error", 0);
            audd_free(parse_err);
            emit_error(callbacks, pe, audd_last_error_message(client));
            result_err = pe;
            audd_http_response_free(&resp);
            break;
        }
        audd_free(parse_err);
        if (m && callbacks->on_match) {
            callbacks->on_match(m, callbacks->user_data);
        }
        if (n && callbacks->on_notification) {
            callbacks->on_notification(n, callbacks->user_data);
        }
        audd_stream_callback_match_free(m);
        audd_stream_callback_notification_free(n);
        if (resp.json) {
            cJSON *ts = cJSON_GetObjectItemCaseSensitive(resp.json, "timestamp");
            if (cJSON_IsNumber(ts)) since_time = (long)ts->valuedouble;
        }
        audd_http_response_free(&resp);
    }

    audd_free(url);
    audd_free(handle);
    if (out_handle) *out_handle = NULL;
    return result_err;
}

audd_error_t audd_longpoll_run_by_radio_id(audd_client_t *client,
                                            int radio_id,
                                            const audd_longpoll_options_t *options,
                                            const audd_longpoll_callbacks_t *callbacks,
                                            audd_longpoll_t **out_handle)
{
    if (client == NULL || callbacks == NULL) {
        return AUDD_ERR_INVALID_ARGUMENT;
    }
    char category[10];
    audd_error_t e = audd_streams_derive_longpoll_category(client, radio_id, category);
    if (e != AUDD_OK) return e;
    return audd_longpoll_run(client, category, options, callbacks, out_handle);
}
