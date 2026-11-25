/* test_longpoll_http.c — the longpoll loop against a local HTTP server.
 *
 * Every request goes to a mock server on 127.0.0.1 (the client's API base
 * is pointed at it). As a guard, https_proxy is set to a closed local port,
 * so a request that tried to reach AudD would fail on this machine. */
#include "audd.h"
#include "audd_internal.h"
#include "unity.h"
#include "test_fixtures.h"
#include "mock_http_server.h"

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *kToken = "unit-test-token-9f3k";
static const char *kCategory = "abc123def";

static mock_server_t g_srv;

/* ------------------------------------------------------------------ *
 * Recording callbacks (they run on the longpoll thread)               *
 * ------------------------------------------------------------------ */

#define MAX_SEEN 16

typedef struct {
    pthread_mutex_t mu;
    int       matches;
    int       notifications;
    int       errors;
    int       retries;
    char      artist[MAX_SEEN][128];
    long long radio_id[MAX_SEEN];
    audd_error_t last_error;
    char      last_error_msg[1100];
    audd_error_t last_retry;
} rec_t;

static rec_t g_rec;

static void on_match(const audd_stream_callback_match_t *m, void *ud)
{
    rec_t *r = (rec_t *)ud;
    pthread_mutex_lock(&r->mu);
    if (r->matches < MAX_SEEN) {
        const char *a = audd_stream_callback_song_get_artist(audd_stream_callback_match_get_song(m));
        snprintf(r->artist[r->matches], 128, "%s", a ? a : "(null)");
        r->radio_id[r->matches] = (long long)audd_stream_callback_match_get_radio_id(m);
    }
    r->matches++;
    pthread_mutex_unlock(&r->mu);
}

static void on_notification(const audd_stream_callback_notification_t *n, void *ud)
{
    (void)n;
    rec_t *r = (rec_t *)ud;
    pthread_mutex_lock(&r->mu);
    r->notifications++;
    pthread_mutex_unlock(&r->mu);
}

static void on_error(audd_error_t err, const char *msg, void *ud)
{
    rec_t *r = (rec_t *)ud;
    pthread_mutex_lock(&r->mu);
    r->errors++;
    r->last_error = err;
    snprintf(r->last_error_msg, sizeof(r->last_error_msg), "%s", msg ? msg : "(null)");
    pthread_mutex_unlock(&r->mu);
}

static void on_retry(audd_error_t err, const char *msg, void *ud)
{
    (void)msg;
    rec_t *r = (rec_t *)ud;
    pthread_mutex_lock(&r->mu);
    r->retries++;
    r->last_retry = err;
    pthread_mutex_unlock(&r->mu);
}

static audd_longpoll_callbacks_t callbacks(void)
{
    audd_longpoll_callbacks_t cb;
    cb.on_match = on_match;
    cb.on_notification = on_notification;
    cb.on_error = on_error;
    cb.user_data = &g_rec;
    return cb;
}

/* ------------------------------------------------------------------ *
 * Running a longpoll on its own thread                                *
 * ------------------------------------------------------------------ */

typedef struct {
    audd_longpoll_t          *handle;
    audd_client_t            *client;
    audd_longpoll_options_t   opts;
    audd_longpoll_callbacks_t cb;
    audd_error_t              result;
    volatile int              done;
    pthread_t                 thread;
} runner_t;

static void *run_main(void *arg)
{
    runner_t *r = (runner_t *)arg;
    r->result = audd_longpoll_run_with_handle(r->handle, r->client, kCategory, &r->opts, &r->cb);
    __atomic_store_n(&r->done, 1, __ATOMIC_SEQ_CST);
    return NULL;
}

static audd_client_t *mock_client(const char *token)
{
    audd_client_t *c = audd_client_new(token, NULL);
    TEST_ASSERT_NOT_NULL(c);
    char base[64];
    snprintf(base, sizeof(base), "http://127.0.0.1:%d", g_srv.port);
    TEST_ASSERT_EQUAL_INT(AUDD_OK, audd_client_set_api_base_for_testing(c, base));
    return c;
}

/* Start a longpoll with a token, the preflight off, a 1 s hold and short
 * pauses. */
static void start_run(runner_t *r, long backoff_start_ms, long backoff_max_ms)
{
    memset(r, 0, sizeof(*r));
    r->handle = audd_longpoll_new();
    TEST_ASSERT_NOT_NULL(r->handle);
    audd_longpoll_set_on_retry(r->handle, on_retry);
    if (backoff_start_ms > 0) {
        audd_longpoll_set_backoff_for_testing(r->handle, backoff_start_ms, backoff_max_ms);
    }
    if (r->client == NULL) r->client = mock_client(kToken);
    r->opts = audd_longpoll_options_default();
    r->opts.timeout = 1;
    r->opts.skip_callback_check = 1;
    r->cb = callbacks();
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&r->thread, NULL, run_main, r));
}

/* Wait (bounded) for the run to return by itself. */
static int wait_done(runner_t *r, long timeout_ms)
{
    long deadline = mock_now_ms() + timeout_ms;
    while (!__atomic_load_n(&r->done, __ATOMIC_SEQ_CST) && mock_now_ms() < deadline) {
        mock_sleep_ms(5);
    }
    return __atomic_load_n(&r->done, __ATOMIC_SEQ_CST);
}

/* Close the handle and wait for the run to return; returns how long that
 * took in ms. A run that doesn't stop fails the test binary outright rather
 * than hanging it. */
static long close_and_join(runner_t *r)
{
    long t0 = mock_now_ms();
    audd_longpoll_close(r->handle);
    if (!wait_done(r, 5000)) {
        fprintf(stderr, "longpoll did not stop within 5 s of audd_longpoll_close\n");
        abort();
    }
    long elapsed = mock_now_ms() - t0;
    pthread_join(r->thread, NULL);
    return elapsed;
}

static void finish(runner_t *r)
{
    audd_longpoll_free(r->handle);
    audd_client_free(r->client);
    r->handle = NULL;
    r->client = NULL;
}

/* ------------------------------------------------------------------ *
 * Fixtures and helpers                                                *
 * ------------------------------------------------------------------ */

static char *g_events;
static char *g_no_events;
static char *g_error;
static char *g_err19;

static const char *req(int i) { return g_srv.reqs[i]; }

static void assert_no_token_in_requests(void)
{
    int n = mock_request_count(&g_srv);
    for (int i = 0; i < n; ++i) {
        TEST_ASSERT_NULL_MESSAGE(strstr(req(i), "api_token"), req(i));
        TEST_ASSERT_NULL_MESSAGE(strstr(req(i), kToken), req(i));
    }
}

void setUp(void)
{
    memset(&g_srv, 0, sizeof(g_srv));
    memset(&g_rec, 0, sizeof(g_rec));
    pthread_mutex_init(&g_rec.mu, NULL);
}

void tearDown(void)
{
    pthread_mutex_destroy(&g_rec.mu);
}

static void serve(const mock_response_t *script, int n, int hold)
{
    for (int i = 0; i < n; ++i) g_srv.script[i] = script[i];
    g_srv.nscript = n;
    g_srv.hold_after_script = hold;
    TEST_ASSERT_EQUAL_INT(0, mock_server_start(&g_srv));
}

/* ------------------------------------------------------------------ *
 * Tests                                                               *
 * ------------------------------------------------------------------ */

void test_events_are_delivered_and_since_time_advances(void)
{
    mock_response_t script[] = { { 200, g_events } };
    serve(script, 1, 1);
    runner_t r;
    start_run(&r, 0, 0);
    TEST_ASSERT_EQUAL_INT(2, mock_wait_requests(&g_srv, 2, 5000));
    close_and_join(&r);

    TEST_ASSERT_EQUAL_INT(AUDD_OK, r.result);
    TEST_ASSERT_EQUAL_INT(0, g_rec.errors);
    TEST_ASSERT_EQUAL_INT(2, g_rec.matches);
    TEST_ASSERT_EQUAL_STRING("Alan Walker, A$AP Rocky", g_rec.artist[0]);
    TEST_ASSERT_EQUAL_STRING("Tears For Fears", g_rec.artist[1]);
    TEST_ASSERT_EQUAL_INT64(7, g_rec.radio_id[0]);
    TEST_ASSERT_EQUAL_INT64(7, g_rec.radio_id[1]);

    TEST_ASSERT_NOT_NULL(strstr(req(0), "GET /longpoll/?category=abc123def&timeout=1 HTTP/1.1"));
    TEST_ASSERT_NULL(strstr(req(0), "since_time"));
    TEST_ASSERT_NOT_NULL_MESSAGE(
        strstr(req(1), "GET /longpoll/?category=abc123def&timeout=1&since_time=1795547548112 "),
        req(1));
    assert_no_token_in_requests();
    mock_server_stop(&g_srv);
    finish(&r);
}

void test_timeout_response_sets_since_time(void)
{
    mock_response_t script[] = { { 200, g_no_events } };
    serve(script, 1, 1);
    runner_t r;
    start_run(&r, 0, 0);
    TEST_ASSERT_EQUAL_INT(2, mock_wait_requests(&g_srv, 2, 5000));
    close_and_join(&r);
    TEST_ASSERT_EQUAL_INT(AUDD_OK, r.result);
    TEST_ASSERT_EQUAL_INT(0, g_rec.matches + g_rec.notifications + g_rec.errors);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(req(1), "since_time=1777901270049 "), req(1));
    mock_server_stop(&g_srv);
    finish(&r);
}

void test_since_time_option_is_sent(void)
{
    mock_response_t script[] = { { 200, g_no_events } };
    serve(script, 0, 1);
    runner_t r;
    memset(&r, 0, sizeof(r));
    r.client = mock_client(kToken);
    r.handle = audd_longpoll_new();
    r.opts = audd_longpoll_options_default();
    r.opts.skip_callback_check = 1;
    r.opts.since_time = 1700000000123L;
    r.cb = callbacks();
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&r.thread, NULL, run_main, &r));
    TEST_ASSERT_EQUAL_INT(1, mock_wait_requests(&g_srv, 1, 5000));
    close_and_join(&r);
    /* Default hold of 50 s when opts.timeout is left at its default. */
    TEST_ASSERT_NOT_NULL_MESSAGE(
        strstr(req(0), "?category=abc123def&timeout=50&since_time=1700000000123 "), req(0));
    mock_server_stop(&g_srv);
    finish(&r);
}

void test_error_body_ends_the_run_after_one_request(void)
{
    mock_response_t script[] = { { 200, g_error } };
    serve(script, 1, 0); /* would answer the same error again */
    runner_t r;
    start_run(&r, 10, 20);
    TEST_ASSERT_TRUE_MESSAGE(wait_done(&r, 5000), "the run must end on an error body");
    pthread_join(r.thread, NULL);
    mock_sleep_ms(200);
    TEST_ASSERT_EQUAL_INT(1, mock_request_count(&g_srv));
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_INVALID_REQUEST, r.result);
    TEST_ASSERT_EQUAL_INT(1, g_rec.errors);
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_INVALID_REQUEST, g_rec.last_error);
    TEST_ASSERT_EQUAL_STRING("Invalid timeout arg.  Must be 1-120.", g_rec.last_error_msg);
    TEST_ASSERT_EQUAL_STRING("Invalid timeout arg.  Must be 1-120.",
                             audd_last_error_message(r.client));
    TEST_ASSERT_EQUAL_INT(0, g_rec.retries);
    mock_server_stop(&g_srv);
    finish(&r);
}

void test_top_level_result_still_delivers(void)
{
    static const char body[] =
        "{\"status\":\"success\",\"result\":{\"radio_id\":5,\"results\":"
        "[{\"artist\":\"Old Shape\",\"title\":\"T\"}]},\"timestamp\":4242}";
    mock_response_t script[] = { { 200, body } };
    serve(script, 1, 1);
    runner_t r;
    start_run(&r, 0, 0);
    TEST_ASSERT_EQUAL_INT(2, mock_wait_requests(&g_srv, 2, 5000));
    close_and_join(&r);
    TEST_ASSERT_EQUAL_INT(1, g_rec.matches);
    TEST_ASSERT_EQUAL_STRING("Old Shape", g_rec.artist[0]);
    TEST_ASSERT_EQUAL_INT64(5, g_rec.radio_id[0]);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(req(1), "since_time=4242 "), req(1));
    mock_server_stop(&g_srv);
    finish(&r);
}

/* Bodies that yield nothing new must not be re-requested in a tight loop. */
static void assert_bounded(const char *body, long window_ms, int max_requests,
                           long backoff_start_ms, long backoff_max_ms)
{
    memset(&g_srv, 0, sizeof(g_srv));
    mock_response_t script[] = { { 200, body } };
    serve(script, 1, 0); /* repeats the body forever */
    runner_t r;
    start_run(&r, backoff_start_ms, backoff_max_ms);
    mock_sleep_ms(window_ms);
    close_and_join(&r);
    int n = mock_request_count(&g_srv);
    char msg[256];
    snprintf(msg, sizeof(msg), "%d requests in %ld ms for body: %s", n, window_ms, body);
    TEST_ASSERT_TRUE_MESSAGE(n >= 1 && n <= max_requests, msg);
    TEST_ASSERT_EQUAL_INT(AUDD_OK, r.result);
    TEST_ASSERT_EQUAL_INT(0, g_rec.errors);
    mock_server_stop(&g_srv);
    finish(&r);
}

void test_junk_bodies_do_not_spin(void)
{
    /* 50 ms doubling to 100 ms: at most ~10 requests in 700 ms. */
    assert_bounded("<html>bad gateway</html>", 700, 15, 50, 100);
    assert_bounded("", 700, 15, 50, 100);
    assert_bounded("{\"events\":[]}", 700, 15, 50, 100);
    assert_bounded("{\"surprise\":1}", 700, 15, 50, 100);
    assert_bounded("{\"timeout\":\"no events before timeout\"}", 700, 15, 50, 100);
    /* The same events again with no newer timestamp. */
    assert_bounded("{\"events\":[{\"data\":{\"result\":{\"results\":[{\"artist\":\"A\"}]}}}]}",
                   700, 15, 50, 100);
}

void test_junk_body_waits_a_second_by_default(void)
{
    /* Default pauses: a request at 0 s, the next after 1 s, then 2 s later. */
    assert_bounded("not json", 1500, 2, 0, 0);
}

void test_server_errors_are_retried_and_reported_through_on_retry(void)
{
    mock_response_t script[] = { { 503, "" }, { 502, "<html>" }, { 200, g_events } };
    serve(script, 3, 1);
    runner_t r;
    start_run(&r, 10, 20);
    TEST_ASSERT_EQUAL_INT(4, mock_wait_requests(&g_srv, 4, 5000));
    close_and_join(&r);
    TEST_ASSERT_EQUAL_INT(AUDD_OK, r.result);
    TEST_ASSERT_EQUAL_INT(2, g_rec.retries);
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_SERVER, g_rec.last_retry);
    TEST_ASSERT_EQUAL_INT(0, g_rec.errors);
    TEST_ASSERT_EQUAL_INT(2, g_rec.matches);
    mock_server_stop(&g_srv);
    finish(&r);
}

void test_repeated_server_errors_end_the_run_once(void)
{
    mock_response_t script[] = { { 503, "" } };
    serve(script, 1, 0);
    runner_t r;
    start_run(&r, 1, 2);
    TEST_ASSERT_TRUE_MESSAGE(wait_done(&r, 5000), "the run must give up");
    pthread_join(r.thread, NULL);
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_SERVER, r.result);
    TEST_ASSERT_EQUAL_INT(9, mock_request_count(&g_srv));
    TEST_ASSERT_EQUAL_INT(8, g_rec.retries);
    TEST_ASSERT_EQUAL_INT(1, g_rec.errors);
    TEST_ASSERT_NOT_NULL(strstr(g_rec.last_error_msg, "HTTP 503"));
    mock_server_stop(&g_srv);
    finish(&r);
}

void test_client_error_status_ends_the_run(void)
{
    mock_response_t script[] = { { 404, "" } };
    serve(script, 1, 0);
    runner_t r;
    start_run(&r, 10, 20);
    TEST_ASSERT_TRUE(wait_done(&r, 5000));
    pthread_join(r.thread, NULL);
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_SERVER, r.result);
    TEST_ASSERT_EQUAL_INT(1, mock_request_count(&g_srv));
    TEST_ASSERT_EQUAL_INT(1, g_rec.errors);
    mock_server_stop(&g_srv);
    finish(&r);
}

void test_close_interrupts_a_waiting_request(void)
{
    serve(NULL, 0, 1); /* never answers */
    runner_t r;
    start_run(&r, 0, 0);
    TEST_ASSERT_EQUAL_INT(1, mock_wait_requests(&g_srv, 1, 5000));
    mock_sleep_ms(100);
    long elapsed = close_and_join(&r);
    TEST_ASSERT_TRUE_MESSAGE(elapsed < 3000, "close must abort the waiting request");
    TEST_ASSERT_EQUAL_INT(AUDD_OK, r.result);
    TEST_ASSERT_EQUAL_INT(0, g_rec.errors);
    mock_server_stop(&g_srv);
    finish(&r);
}

void test_close_interrupts_a_pause(void)
{
    mock_response_t script[] = { { 200, "junk" } };
    serve(script, 1, 0);
    runner_t r;
    start_run(&r, 20000, 30000);
    TEST_ASSERT_EQUAL_INT(1, mock_wait_requests(&g_srv, 1, 5000));
    mock_sleep_ms(100);
    long elapsed = close_and_join(&r);
    TEST_ASSERT_TRUE_MESSAGE(elapsed < 1000, "close must cut the pause short");
    TEST_ASSERT_EQUAL_INT(1, mock_request_count(&g_srv));
    mock_server_stop(&g_srv);
    finish(&r);
}

void test_client_close_stops_the_run(void)
{
    serve(NULL, 0, 1);
    runner_t r;
    start_run(&r, 0, 0);
    TEST_ASSERT_EQUAL_INT(1, mock_wait_requests(&g_srv, 1, 5000));
    long t0 = mock_now_ms();
    audd_client_close(r.client);
    TEST_ASSERT_TRUE(wait_done(&r, 5000));
    TEST_ASSERT_TRUE(mock_now_ms() - t0 < 3000);
    pthread_join(r.thread, NULL);
    TEST_ASSERT_EQUAL_INT(AUDD_OK, r.result);
    TEST_ASSERT_EQUAL_INT(0, g_rec.errors);
    mock_server_stop(&g_srv);
    finish(&r);
}

void test_closed_handle_returns_without_a_request(void)
{
    serve(NULL, 0, 1);
    audd_client_t *c = mock_client(kToken);
    audd_longpoll_t *h = audd_longpoll_new();
    audd_longpoll_close(h);
    audd_longpoll_close(h); /* idempotent */
    audd_longpoll_callbacks_t cb = callbacks();
    TEST_ASSERT_EQUAL_INT(AUDD_OK, audd_longpoll_run_with_handle(h, c, kCategory, NULL, &cb));
    mock_sleep_ms(100);
    TEST_ASSERT_EQUAL_INT(0, mock_request_count(&g_srv));
    TEST_ASSERT_EQUAL_INT(0, g_rec.errors);
    audd_longpoll_free(h);
    audd_client_free(c);
    mock_server_stop(&g_srv);
}

void test_tokenless_consumer_skips_the_preflight(void)
{
    mock_response_t script[] = { { 200, g_no_events } };
    serve(script, 1, 1);
    runner_t r;
    memset(&r, 0, sizeof(r));
    r.client = mock_client(NULL); /* AUDD_API_TOKEN is unset in main() */
    TEST_ASSERT_NULL(audd_client_api_token(r.client));
    r.handle = audd_longpoll_new();
    r.opts = audd_longpoll_options_default(); /* skip_callback_check = 0 */
    r.opts.timeout = 1;
    r.cb = callbacks();
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&r.thread, NULL, run_main, &r));
    TEST_ASSERT_EQUAL_INT(2, mock_wait_requests(&g_srv, 2, 5000));
    close_and_join(&r);
    TEST_ASSERT_EQUAL_INT(AUDD_OK, r.result);
    TEST_ASSERT_EQUAL_INT(0, g_rec.errors);
    TEST_ASSERT_NOT_NULL(strstr(req(0), "GET /longpoll/"));
    mock_server_stop(&g_srv);
    finish(&r);
}

void test_preflight_without_callback_url_is_an_invalid_request(void)
{
    mock_response_t script[] = { { 200, g_err19 } };
    serve(script, 1, 0);
    audd_client_t *c = mock_client(kToken);
    audd_longpoll_t *h = audd_longpoll_new();
    audd_longpoll_callbacks_t cb = callbacks();
    audd_error_t e = audd_longpoll_run_with_handle(h, c, kCategory, NULL, &cb);
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_INVALID_REQUEST, e);
    TEST_ASSERT_EQUAL_INT(1, g_rec.errors);
    TEST_ASSERT_NOT_NULL(strstr(g_rec.last_error_msg, "no callback URL is configured"));
    TEST_ASSERT_EQUAL_INT(1, mock_request_count(&g_srv));
    TEST_ASSERT_NOT_NULL(strstr(req(0), "POST /getCallbackUrl/"));
    audd_longpoll_free(h);
    audd_client_free(c);
    mock_server_stop(&g_srv);
}

void test_preflight_with_callback_url_then_polls(void)
{
    mock_response_t script[] = {
        { 200, "{\"status\":\"success\",\"result\":\"https://audd.tech/empty/\"}" },
        { 200, g_no_events },
    };
    serve(script, 2, 1);
    runner_t r;
    memset(&r, 0, sizeof(r));
    r.client = mock_client(kToken);
    r.handle = audd_longpoll_new();
    r.opts = audd_longpoll_options_default();
    r.opts.timeout = 1;
    r.cb = callbacks();
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&r.thread, NULL, run_main, &r));
    TEST_ASSERT_EQUAL_INT(3, mock_wait_requests(&g_srv, 3, 5000));
    close_and_join(&r);
    TEST_ASSERT_EQUAL_INT(AUDD_OK, r.result);
    TEST_ASSERT_NOT_NULL(strstr(req(0), "POST /getCallbackUrl/"));
    TEST_ASSERT_NOT_NULL(strstr(req(1), "GET /longpoll/"));
    /* The preflight is an authenticated call; the polls are not. */
    TEST_ASSERT_NOT_NULL(strstr(req(0), kToken));
    TEST_ASSERT_NULL(strstr(req(1), kToken));
    TEST_ASSERT_NULL(strstr(req(2), kToken));
    TEST_ASSERT_NULL(strstr(req(1), "api_token"));
    mock_server_stop(&g_srv);
    finish(&r);
}

void test_invalid_arguments(void)
{
    audd_client_t *c = audd_client_new(kToken, NULL);
    audd_longpoll_t *h = audd_longpoll_new();
    audd_longpoll_callbacks_t cb = callbacks();
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_INVALID_ARGUMENT, audd_longpoll_run_with_handle(NULL, c, kCategory, NULL, &cb));
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_INVALID_ARGUMENT, audd_longpoll_run_with_handle(h, NULL, kCategory, NULL, &cb));
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_INVALID_ARGUMENT, audd_longpoll_run_with_handle(h, c, NULL, NULL, &cb));
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_INVALID_ARGUMENT, audd_longpoll_run_with_handle(h, c, kCategory, NULL, NULL));
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_INVALID_ARGUMENT, audd_longpoll_run(NULL, kCategory, NULL, &cb, NULL));
    TEST_ASSERT_EQUAL_INT(0, g_rec.errors);
    audd_longpoll_close(NULL);
    audd_longpoll_free(NULL);
    audd_longpoll_set_on_retry(NULL, on_retry);
    audd_longpoll_free(h);
    audd_client_free(c);
}

void test_by_radio_id_without_token_reports_once(void)
{
    audd_client_t *c = audd_client_new(NULL, NULL);
    audd_longpoll_t *h = audd_longpoll_new();
    audd_longpoll_callbacks_t cb = callbacks();
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_AUTHENTICATION,
        audd_longpoll_run_by_radio_id_with_handle(h, c, 1, NULL, &cb));
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_AUTHENTICATION,
        audd_longpoll_run_by_radio_id(c, 1, NULL, &cb, NULL));
    TEST_ASSERT_EQUAL_INT(2, g_rec.errors);
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_AUTHENTICATION, g_rec.last_error);
    audd_longpoll_free(h);
    audd_client_free(c);
}

typedef struct {
    audd_client_t            *client;
    audd_longpoll_t          *handle; /* written by audd_longpoll_run */
    audd_longpoll_options_t   opts;
    audd_longpoll_callbacks_t cb;
    audd_error_t              result;
    volatile int              done;
} run_owned_t;

static void *run_owned_main(void *arg)
{
    run_owned_t *o = (run_owned_t *)arg;
    o->result = audd_longpoll_run(o->client, kCategory, &o->opts, &o->cb, &o->handle);
    __atomic_store_n(&o->done, 1, __ATOMIC_SEQ_CST);
    return NULL;
}

void test_run_owned_handle_polls_and_closes(void)
{
    mock_response_t script[] = { { 200, g_events } };
    serve(script, 1, 1);
    run_owned_t o;
    memset(&o, 0, sizeof(o));
    o.client = mock_client(kToken);
    o.opts = audd_longpoll_options_default();
    o.opts.timeout = 1;
    o.opts.skip_callback_check = 1;
    o.cb = callbacks();
    pthread_t th;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&th, NULL, run_owned_main, &o));
    /* The second request is held open, so the run is still going. */
    TEST_ASSERT_EQUAL_INT(2, mock_wait_requests(&g_srv, 2, 5000));
    audd_longpoll_t *h = __atomic_load_n(&o.handle, __ATOMIC_SEQ_CST);
    TEST_ASSERT_NOT_NULL(h);
    audd_longpoll_close(h);
    long deadline = mock_now_ms() + 5000;
    while (!__atomic_load_n(&o.done, __ATOMIC_SEQ_CST) && mock_now_ms() < deadline) {
        mock_sleep_ms(5);
    }
    if (!__atomic_load_n(&o.done, __ATOMIC_SEQ_CST)) abort();
    pthread_join(th, NULL);
    TEST_ASSERT_EQUAL_INT(AUDD_OK, o.result);
    TEST_ASSERT_NULL(o.handle);
    TEST_ASSERT_EQUAL_INT(2, g_rec.matches);
    TEST_ASSERT_NOT_NULL(strstr(req(1), "since_time=1795547548112 "));
    assert_no_token_in_requests();
    audd_client_free(o.client);
    mock_server_stop(&g_srv);
}

int main(void)
{
    /* A write to a connection the client already closed must not end the
     * test binary. */
    signal(SIGPIPE, SIG_IGN);
    unsetenv("AUDD_API_TOKEN");
    unsetenv("http_proxy");
    unsetenv("HTTP_PROXY");
    unsetenv("all_proxy");
    unsetenv("ALL_PROXY");
    setenv("https_proxy", "http://127.0.0.1:1", 1);
    setenv("HTTPS_PROXY", "http://127.0.0.1:1", 1);
    setenv("no_proxy", "127.0.0.1", 1);
    setenv("NO_PROXY", "127.0.0.1", 1);

    g_events = read_fixture("longpoll_events.json", NULL);
    g_no_events = read_fixture("longpoll_no_events.json", NULL);
    g_error = read_fixture("longpoll_error.json", NULL);
    g_err19 = read_fixture("error_19_no_callback_url.json", NULL);
    if (!g_events || !g_no_events || !g_error || !g_err19) {
        fprintf(stderr, "missing fixtures under %s\n", AUDD_TEST_FIXTURES_DIR);
        return 1;
    }

    UNITY_BEGIN();
    RUN_TEST(test_events_are_delivered_and_since_time_advances);
    RUN_TEST(test_timeout_response_sets_since_time);
    RUN_TEST(test_since_time_option_is_sent);
    RUN_TEST(test_error_body_ends_the_run_after_one_request);
    RUN_TEST(test_top_level_result_still_delivers);
    RUN_TEST(test_junk_bodies_do_not_spin);
    RUN_TEST(test_junk_body_waits_a_second_by_default);
    RUN_TEST(test_server_errors_are_retried_and_reported_through_on_retry);
    RUN_TEST(test_repeated_server_errors_end_the_run_once);
    RUN_TEST(test_client_error_status_ends_the_run);
    RUN_TEST(test_close_interrupts_a_waiting_request);
    RUN_TEST(test_close_interrupts_a_pause);
    RUN_TEST(test_client_close_stops_the_run);
    RUN_TEST(test_closed_handle_returns_without_a_request);
    RUN_TEST(test_tokenless_consumer_skips_the_preflight);
    RUN_TEST(test_preflight_without_callback_url_is_an_invalid_request);
    RUN_TEST(test_preflight_with_callback_url_then_polls);
    RUN_TEST(test_invalid_arguments);
    RUN_TEST(test_by_radio_id_without_token_reports_once);
    RUN_TEST(test_run_owned_handle_polls_and_closes);
    int rc = UNITY_END();

    free(g_events);
    free(g_no_events);
    free(g_error);
    free(g_err19);
    return rc;
}
