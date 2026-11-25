/* audd_longpoll.c — blocking longpoll consumer with callback dispatch.
 *
 * GET https://api.audd.io/longpoll/?category=…&timeout=…&since_time=…
 * answers HTTP 200 with one of:
 *
 *   {"events":[{"timestamp":<ms>,"category":"…","data":<callback body>}, …]}
 *       Each event's `data` is the body a callback URL receives (a
 *       recognition or a notification), possibly sent as a JSON string. The
 *       largest event timestamp is the next since_time.
 *   {"timeout":"no events before timeout","timestamp":<ms>}
 *       Nothing new; the timestamp is the next since_time.
 *   {"error":"…"}
 *       The request itself is invalid (for example a timeout outside
 *       1-120). Sending it again gets the same answer, so it ends the run.
 *
 * A callback body at the top level ({"result":…} or {"notification":…},
 * with an optional top-level timestamp) is delivered as well.
 *
 * The request carries no api_token: the category authorizes it.
 */
#include "audd_internal.h"
#include "audd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../vendor/cJSON/cJSON.h"

/* How many transient failures in a row the loop retries; the next one ends
 * the run through on_error. */
static const int kMaxConsecutiveFailures = 8;
/* Extra socket slack over the server-side hold so the poll doesn't time out
 * before the server releases the request. */
static const int kPollTimeoutMarginSeconds = 15;
/* Server-side hold used when options.timeout is 0 or negative. */
static const int kDefaultHoldSeconds = 50;

struct audd_longpoll {
    /* Set by audd_longpoll_close; read with AUDD_FLAG_GET. */
    volatile int closed;
    /* Optional; reports each transient failure the loop retries. */
    audd_on_error_fn on_retry;
    /* Pause bounds; see audd_longpoll_set_backoff_for_testing. */
    long  backoff_start_ms;
    long  backoff_max_ms;
};

long audd_longpoll_poll_timeout(long standard_timeout_seconds,
                                int hold_timeout_seconds)
{
    long need = (long)hold_timeout_seconds + kPollTimeoutMarginSeconds;
    long base = standard_timeout_seconds > 0 ? standard_timeout_seconds : 60;
    return need > base ? need : base;
}

audd_longpoll_action_t audd_longpoll_classify_http(int rc, long status)
{
    if (rc != 0) {
        /* Connection / timeout-class failure — transient. */
        return AUDD_LONGPOLL_RETRY;
    }
    if (status >= 400) {
        if (status == 408 || status == 429 || status >= 500) {
            return AUDD_LONGPOLL_RETRY;
        }
        return AUDD_LONGPOLL_TERMINAL; /* auth / other client error */
    }
    return AUDD_LONGPOLL_CONTINUE;
}

long audd_longpoll_next_backoff(long current, long max_ms)
{
    if (current <= 0) current = 1;
    return current >= max_ms / 2 ? max_ms : current * 2;
}

/* ------------------------------------------------------------------ *
 * Handle                                                              *
 * ------------------------------------------------------------------ */

audd_longpoll_options_t audd_longpoll_options_default(void)
{
    audd_longpoll_options_t o;
    o.since_time = 0;
    o.timeout = kDefaultHoldSeconds;
    o.skip_callback_check = 0;
    return o;
}

audd_longpoll_t *audd_longpoll_new(void)
{
    audd_longpoll_t *h = (audd_longpoll_t *)audd_malloc(sizeof(*h));
    if (h == NULL) return NULL;
    memset(h, 0, sizeof(*h));
    h->backoff_start_ms = AUDD_LONGPOLL_BACKOFF_START_MS;
    h->backoff_max_ms = AUDD_LONGPOLL_BACKOFF_MAX_MS;
    return h;
}

void audd_longpoll_free(audd_longpoll_t *handle)
{
    audd_free(handle);
}

void audd_longpoll_close(audd_longpoll_t *handle)
{
    if (handle == NULL) return;
    AUDD_FLAG_SET(&handle->closed);
}

void audd_longpoll_set_on_retry(audd_longpoll_t *handle, audd_on_error_fn on_retry)
{
    if (handle == NULL) return;
    handle->on_retry = on_retry;
}

void audd_longpoll_set_backoff_for_testing(audd_longpoll_t *handle,
                                           long start_ms,
                                           long max_ms)
{
    if (handle == NULL) return;
    handle->backoff_start_ms = start_ms > 0 ? start_ms : AUDD_LONGPOLL_BACKOFF_START_MS;
    handle->backoff_max_ms = max_ms > 0 ? max_ms : AUDD_LONGPOLL_BACKOFF_MAX_MS;
    if (handle->backoff_max_ms < handle->backoff_start_ms) {
        handle->backoff_max_ms = handle->backoff_start_ms;
    }
}

static int should_stop(audd_longpoll_t *handle, audd_client_t *client)
{
    if (handle != NULL && AUDD_FLAG_GET(&handle->closed) != 0) return 1;
    return audd_client_is_closed(client);
}

/* ------------------------------------------------------------------ *
 * Response interpretation                                             *
 * ------------------------------------------------------------------ */

/* A millisecond timestamp: a JSON number or a numeric string, > 0. */
static int read_timestamp(const cJSON *node, int64_t *out)
{
    int64_t v = 0;
    if (!audd_json_node_to_int64(node, &v) || v <= 0) return 0;
    *out = v;
    return 1;
}

/* Parse one callback body and hand the result to the matching callback.
 * Returns 1 when something was delivered. Anything that doesn't parse
 * (including a recognition with an empty `results` list, and an allocation
 * failure) is skipped. */
static int deliver_callback_body(const char *body, size_t len,
                                 const audd_longpoll_callbacks_t *cb)
{
    audd_stream_callback_match_t *m = NULL;
    audd_stream_callback_notification_t *n = NULL;
    char *parse_err = NULL;
    audd_error_t pe = audd_parse_callback(body, len, &m, &n, &parse_err);
    audd_free(parse_err);
    if (pe != AUDD_OK) {
        audd_stream_callback_match_free(m);
        audd_stream_callback_notification_free(n);
        return 0;
    }
    if (m != NULL && cb->on_match != NULL) {
        cb->on_match(m, cb->user_data);
    }
    if (n != NULL && cb->on_notification != NULL) {
        cb->on_notification(n, cb->user_data);
    }
    int delivered = (m != NULL || n != NULL);
    audd_stream_callback_match_free(m);
    audd_stream_callback_notification_free(n);
    return delivered;
}

/* An event's `data`: an object, or the same JSON sent as a string. */
static void deliver_event_data(const cJSON *data, const audd_longpoll_callbacks_t *cb)
{
    if (cJSON_IsString(data)) {
        if (data->valuestring == NULL) return;
        (void)deliver_callback_body(data->valuestring, strlen(data->valuestring), cb);
        return;
    }
    if (!cJSON_IsObject(data)) return;
    char *printed = cJSON_PrintUnformatted(data);
    if (printed == NULL) return; /* out of memory: skip the event */
    (void)deliver_callback_body(printed, strlen(printed), cb);
    audd_free(printed);
}

/* Fill page->error* from an `error` member that is present and not null. */
static void read_error_body(const cJSON *root, const cJSON *err, audd_longpoll_page_t *page)
{
    const char *text = NULL;
    char *printed = NULL;
    page->error = AUDD_ERR_INVALID_REQUEST;
    page->error_code = 0;
    if (cJSON_IsObject(err)) {
        /* An AudD error block: {"error":{"error_code":N,"error_message":"…"}}. */
        int code = audd_response_error_code(root);
        text = audd_json_get_string(err, "error_message");
        if (code != 0) {
            page->error = audd_sentinel_for_code(code);
            page->error_code = code;
            snprintf(page->error_message, sizeof(page->error_message),
                     "[#%d] %s", code, text ? text : "");
            return;
        }
    } else if (cJSON_IsString(err)) {
        text = err->valuestring;
    } else {
        printed = cJSON_PrintUnformatted(err);
        text = printed;
    }
    /* Trim surrounding whitespace for the empty check. */
    const char *p = text;
    while (p != NULL && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    snprintf(page->error_message, sizeof(page->error_message), "%s",
             (p != NULL && *p != '\0') ? text : "Longpoll request rejected");
    audd_free(printed);
}

void audd_longpoll_process_body(const char *body,
                                size_t len,
                                const cJSON *json,
                                const audd_longpoll_callbacks_t *callbacks,
                                audd_longpoll_t *handle,
                                audd_client_t *client,
                                audd_longpoll_page_t *page)
{
    if (page == NULL) return;
    page->progressed = 0;
    page->error = AUDD_OK;
    page->error_code = 0;
    page->error_message[0] = '\0';
    if (callbacks == NULL || !cJSON_IsObject(json)) {
        return; /* empty, non-JSON or not an object: nothing new */
    }

    const cJSON *err = cJSON_GetObjectItemCaseSensitive(json, "error");
    if (err != NULL && !cJSON_IsNull(err)) {
        read_error_body(json, err, page);
        return;
    }

    const cJSON *events = cJSON_GetObjectItemCaseSensitive(json, "events");
    if (events != NULL) {
        /* An events response advances only through its timestamps: without
         * a newer since_time the next request would return the same events. */
        int64_t newest = 0;
        int have_newest = 0;
        if (cJSON_IsArray(events)) {
            for (const cJSON *ev = events->child; ev != NULL; ev = ev->next) {
                if (should_stop(handle, client)) break;
                if (!cJSON_IsObject(ev)) continue;
                int64_t ts = 0;
                if (read_timestamp(cJSON_GetObjectItemCaseSensitive(ev, "timestamp"), &ts) &&
                    (!have_newest || ts > newest)) {
                    newest = ts;
                    have_newest = 1;
                }
                deliver_event_data(cJSON_GetObjectItemCaseSensitive(ev, "data"), callbacks);
            }
        }
        if (have_newest && newest != page->since) {
            page->since = newest;
            page->progressed = 1;
        }
        return;
    }

    int64_t ts = 0;
    int have_ts = read_timestamp(cJSON_GetObjectItemCaseSensitive(json, "timestamp"), &ts);
    if (cJSON_GetObjectItemCaseSensitive(json, "result") != NULL ||
        cJSON_GetObjectItemCaseSensitive(json, "notification") != NULL) {
        if (body != NULL && len > 0 && deliver_callback_body(body, len, callbacks)) {
            page->progressed = 1;
        }
    } else if (cJSON_GetObjectItemCaseSensitive(json, "timeout") == NULL) {
        return; /* unknown shape: nothing new */
    }
    if (have_ts && ts != page->since) {
        page->since = ts;
        page->progressed = 1;
    }
}

/* ------------------------------------------------------------------ *
 * Loop                                                                *
 * ------------------------------------------------------------------ */

static void emit(audd_on_error_fn fn, audd_error_t err, const char *msg, void *ud)
{
    if (fn == NULL) return;
    fn(err, (msg != NULL && msg[0] != '\0') ? msg : audd_error_string(err), ud);
}

/* End the run with an error: record it on the client (for
 * audd_last_error_message) and report it through on_error, once. */
static audd_error_t fail(audd_client_t *client, const audd_longpoll_callbacks_t *cb,
                         audd_error_t err, const char *msg, int api_code)
{
    audd_client_set_error(client, (msg != NULL && msg[0] != '\0') ? msg : audd_error_string(err),
                          api_code);
    if (cb != NULL) emit(cb->on_error, err, msg, cb->user_data);
    return err;
}

/* Catch the silent-failure mode: with no callback URL configured for the
 * account, longpoll never delivers events. */
static audd_error_t preflight_callback_url(audd_client_t *client,
                                           char *msg, size_t msg_len, int *api_code)
{
    char *url = NULL;
    int code = 0;
    audd_error_t e = audd_streams_get_callback_url_ex(client, &url, &code);
    int missing = (e == AUDD_OK && (url == NULL || url[0] == '\0')) ||
                  (e != AUDD_OK && code == AUDD_NO_CALLBACK_URL_CODE);
    audd_free(url);
    *api_code = code;
    if (missing) {
        snprintf(msg, msg_len, "%s",
            "Longpoll won't deliver events because no callback URL is "
            "configured for this account. Set one first via "
            "audd_streams_set_callback_url(client, url, opts) — "
            "\"https://audd.tech/empty/\" is fine if you only want "
            "longpolling and don't need a real receiver. To skip this "
            "check, set audd_longpoll_options_t.skip_callback_check = 1.");
        return AUDD_ERR_INVALID_REQUEST;
    }
    if (e != AUDD_OK) {
        snprintf(msg, msg_len, "%s", audd_last_error_message(client));
    }
    return e;
}

static audd_error_t run_loop(audd_longpoll_t *handle,
                             audd_client_t *client,
                             const char *category,
                             const audd_longpoll_options_t *options,
                             const audd_longpoll_callbacks_t *cb)
{
    audd_longpoll_options_t o = options ? *options : audd_longpoll_options_default();
    if (o.timeout <= 0) o.timeout = kDefaultHoldSeconds;
    char msg[1024];
    msg[0] = '\0';

    if (should_stop(handle, client)) return AUDD_OK;

    /* The preflight needs the account's token; a tokenless consumer (one that
     * received only a category) has none to check with. */
    char *token = audd_client_copy_api_token(client);
    int has_token = token != NULL;
    audd_free(token);
    if (!o.skip_callback_check && has_token) {
        int code = 0;
        audd_error_t pe = preflight_callback_url(client, msg, sizeof(msg), &code);
        if (pe != AUDD_OK) {
            if (should_stop(handle, client)) return AUDD_OK;
            return fail(client, cb, pe, msg, code);
        }
    }

    char *url = audd_aprintf("%s/longpoll/", audd_client_api_base(client));
    if (url == NULL) return fail(client, cb, AUDD_ERR_OUT_OF_MEMORY, "audd: out of memory", 0);

    long poll_timeout = audd_longpoll_poll_timeout(
        client->options.standard_timeout_seconds, o.timeout);

    int64_t since = o.since_time > 0 ? (int64_t)o.since_time : 0;
    audd_error_t result = AUDD_OK;
    int consecutive_failures = 0;
    long failure_backoff = handle->backoff_start_ms;
    long idle_backoff = handle->backoff_start_ms;
    audd_longpoll_page_t page;

    while (!should_stop(handle, client)) {
        char timeout_buf[16];
        snprintf(timeout_buf, sizeof(timeout_buf), "%d", o.timeout);
        char since_buf[24];
        const char *kv[8];
        size_t k = 0;
        kv[k++] = "category"; kv[k++] = category;
        kv[k++] = "timeout";  kv[k++] = timeout_buf;
        if (since > 0) {
            snprintf(since_buf, sizeof(since_buf), "%lld", (long long)since);
            kv[k++] = "since_time"; kv[k++] = since_buf;
        }
        kv[k] = NULL;

        audd_http_response_t resp;
        memset(&resp, 0, sizeof(resp));
        int rc = audd_http_get_ex(client, url, kv, poll_timeout, &handle->closed,
                                  &resp, msg, sizeof(msg));
        if (should_stop(handle, client)) {
            audd_http_response_free(&resp);
            break;
        }

        audd_longpoll_action_t action = audd_longpoll_classify_http(rc, resp.status);
        if (action == AUDD_LONGPOLL_TERMINAL) {
            snprintf(msg, sizeof(msg), "Longpoll endpoint returned HTTP %ld", resp.status);
            audd_http_response_free(&resp);
            result = fail(client, cb, AUDD_ERR_SERVER, msg, 0);
            break;
        }
        if (action == AUDD_LONGPOLL_RETRY) {
            audd_error_t err = AUDD_ERR_CONNECTION;
            if (rc == 0) {
                err = AUDD_ERR_SERVER;
                snprintf(msg, sizeof(msg), "Longpoll endpoint returned HTTP %ld", resp.status);
            }
            audd_http_response_free(&resp);
            if (++consecutive_failures > kMaxConsecutiveFailures) {
                char final_msg[1200];
                snprintf(final_msg, sizeof(final_msg),
                         "Longpoll gave up after %d consecutive failures: %s",
                         consecutive_failures, msg);
                result = fail(client, cb, err, final_msg, 0);
                break;
            }
            emit(handle->on_retry, err, msg, cb->user_data);
            if (!audd_wait_ms(client, &handle->closed, failure_backoff)) break;
            failure_backoff = audd_longpoll_next_backoff(failure_backoff, handle->backoff_max_ms);
            continue;
        }

        /* Got an HTTP response: the failure streak is over. */
        consecutive_failures = 0;
        failure_backoff = handle->backoff_start_ms;

        page.since = since;
        audd_longpoll_process_body(resp.body, resp.body_len, resp.json, cb,
                                   handle, client, &page);
        audd_http_response_free(&resp);
        if (page.error != AUDD_OK) {
            result = fail(client, cb, page.error, page.error_message, page.error_code);
            break;
        }
        since = page.since;
        if (page.progressed) {
            idle_backoff = handle->backoff_start_ms;
            continue;
        }
        /* Nothing new (an empty events list, an unknown shape, an empty or
         * non-JSON body): wait before asking again. */
        if (!audd_wait_ms(client, &handle->closed, idle_backoff)) break;
        idle_backoff = audd_longpoll_next_backoff(idle_backoff, handle->backoff_max_ms);
    }

    audd_free(url);
    return result;
}

/* ------------------------------------------------------------------ *
 * Public entry points                                                 *
 * ------------------------------------------------------------------ */

audd_error_t audd_longpoll_run_with_handle(audd_longpoll_t *handle,
                                           audd_client_t *client,
                                           const char *category,
                                           const audd_longpoll_options_t *options,
                                           const audd_longpoll_callbacks_t *callbacks)
{
    if (handle == NULL || client == NULL || category == NULL || callbacks == NULL) {
        return AUDD_ERR_INVALID_ARGUMENT;
    }
    return run_loop(handle, client, category, options, callbacks);
}

audd_error_t audd_longpoll_run_by_radio_id_with_handle(audd_longpoll_t *handle,
                                                       audd_client_t *client,
                                                       int radio_id,
                                                       const audd_longpoll_options_t *options,
                                                       const audd_longpoll_callbacks_t *callbacks)
{
    if (handle == NULL || client == NULL || callbacks == NULL) {
        return AUDD_ERR_INVALID_ARGUMENT;
    }
    char category[10];
    audd_error_t e = audd_streams_derive_longpoll_category(client, radio_id, category);
    if (e != AUDD_OK) {
        return fail(client, callbacks, e,
                    "audd: audd_longpoll_run_by_radio_id needs a client with an api_token; "
                    "a consumer without one passes the category to audd_longpoll_run", 0);
    }
    return run_loop(handle, client, category, options, callbacks);
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
    audd_longpoll_t *handle = audd_longpoll_new();
    if (handle == NULL) {
        return fail(client, callbacks, AUDD_ERR_OUT_OF_MEMORY, "audd: out of memory", 0);
    }
    if (out_handle) *out_handle = handle;
    audd_error_t e = run_loop(handle, client, category, options, callbacks);
    if (out_handle) *out_handle = NULL;
    audd_longpoll_free(handle);
    return e;
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
    if (e != AUDD_OK) {
        return fail(client, callbacks, e,
                    "audd: audd_longpoll_run_by_radio_id needs a client with an api_token; "
                    "a consumer without one passes the category to audd_longpoll_run", 0);
    }
    return audd_longpoll_run(client, category, options, callbacks, out_handle);
}
