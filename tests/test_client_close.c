/* test_client_close.c — audd_client_close, plus stream and custom-catalog
 * calls against a local HTTP server.
 *
 * Every request goes to a mock server on 127.0.0.1 (the client's API base
 * is pointed at it), or never leaves the client because it is closed. As a
 * guard, https_proxy is set to a closed local port, so a request that tried
 * to reach AudD would fail on this machine. */
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

static mock_server_t g_srv;

void setUp(void) { memset(&g_srv, 0, sizeof(g_srv)); }
void tearDown(void) {}

static void serve(const mock_response_t *script, int n, int hold)
{
    for (int i = 0; i < n; ++i) g_srv.script[i] = script[i];
    g_srv.nscript = n;
    g_srv.hold_after_script = hold;
    TEST_ASSERT_EQUAL_INT(0, mock_server_start(&g_srv));
}

static audd_client_t *mock_client(long backoff_ms)
{
    audd_options_t o = audd_options_default();
    o.backoff_ms = backoff_ms;
    audd_client_t *c = audd_client_new("unit-test-token-9f3k", &o);
    TEST_ASSERT_NOT_NULL(c);
    char base[64];
    snprintf(base, sizeof(base), "http://127.0.0.1:%d", g_srv.port);
    TEST_ASSERT_EQUAL_INT(AUDD_OK, audd_client_set_api_base_for_testing(c, base));
    return c;
}

/* ------------------------------------------------------------------ *
 * A closed client                                                     *
 * ------------------------------------------------------------------ */

void test_closed_client_returns_closed_without_a_request(void)
{
    audd_client_t *c = audd_client_new("unit-test-token-9f3k", NULL);
    TEST_ASSERT_NOT_NULL(c);
    audd_client_close(c);
    audd_client_close(c); /* idempotent */
    audd_client_close(NULL);

    audd_recognition_t *r = NULL;
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_CLOSED,
        audd_recognize(c, "https://example.com/clip.mp3", NULL, &r));
    TEST_ASSERT_NULL(r);
    TEST_ASSERT_EQUAL_STRING("audd: client closed", audd_last_error_message(c));

    char *url = NULL;
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_CLOSED, audd_streams_get_callback_url(c, &url));
    TEST_ASSERT_NULL(url);

    /* An http(s) source passes the custom-catalog source check (the call
     * then stops because the client is closed); a missing path doesn't. */
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_CLOSED,
        audd_custom_catalog_add(c, 1, "https://example.com/song.mp3"));
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_INVALID_ARGUMENT,
        audd_custom_catalog_add(c, 1, "/no/such/file.mp3"));
    audd_client_free(c);
}

/* ------------------------------------------------------------------ *
 * Closing while a call runs on another thread                         *
 * ------------------------------------------------------------------ */

typedef struct {
    audd_client_t *client;
    audd_error_t   result;
    volatile int   done;
} call_t;

static void *list_streams_main(void *arg)
{
    call_t *call = (call_t *)arg;
    audd_stream_list_t *list = NULL;
    call->result = audd_streams_list(call->client, &list);
    audd_stream_list_free(list);
    __atomic_store_n(&call->done, 1, __ATOMIC_SEQ_CST);
    return NULL;
}

static long close_and_wait(call_t *call, pthread_t th)
{
    long t0 = mock_now_ms();
    audd_client_close(call->client);
    long deadline = t0 + 5000;
    while (!__atomic_load_n(&call->done, __ATOMIC_SEQ_CST) && mock_now_ms() < deadline) {
        mock_sleep_ms(5);
    }
    if (!__atomic_load_n(&call->done, __ATOMIC_SEQ_CST)) {
        fprintf(stderr, "call did not stop within 5 s of audd_client_close\n");
        abort();
    }
    long elapsed = mock_now_ms() - t0;
    pthread_join(th, NULL);
    return elapsed;
}

void test_close_aborts_a_waiting_request(void)
{
    serve(NULL, 0, 1); /* never answers */
    call_t call = { mock_client(1), AUDD_OK, 0 };
    pthread_t th;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&th, NULL, list_streams_main, &call));
    TEST_ASSERT_EQUAL_INT(1, mock_wait_requests(&g_srv, 1, 5000));
    mock_sleep_ms(100);
    long elapsed = close_and_wait(&call, th);
    TEST_ASSERT_TRUE_MESSAGE(elapsed < 3000, "close must abort the request");
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_CLOSED, call.result);
    TEST_ASSERT_EQUAL_STRING("audd: client closed", audd_last_error_message(call.client));
    mock_sleep_ms(100);
    TEST_ASSERT_EQUAL_INT(1, mock_request_count(&g_srv));
    audd_client_free(call.client);
    mock_server_stop(&g_srv);
}

void test_close_cuts_a_retry_pause_short(void)
{
    mock_response_t script[] = { { 503, "" } };
    serve(script, 1, 0);
    call_t call = { mock_client(20000), AUDD_OK, 0 };
    pthread_t th;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&th, NULL, list_streams_main, &call));
    TEST_ASSERT_EQUAL_INT(1, mock_wait_requests(&g_srv, 1, 5000));
    mock_sleep_ms(100);
    long elapsed = close_and_wait(&call, th);
    TEST_ASSERT_TRUE_MESSAGE(elapsed < 1000, "close must cut the retry pause short");
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_CLOSED, call.result);
    TEST_ASSERT_EQUAL_INT(1, mock_request_count(&g_srv));
    audd_client_free(call.client);
    mock_server_stop(&g_srv);
}

/* ------------------------------------------------------------------ *
 * Custom catalog                                                      *
 * ------------------------------------------------------------------ */

void test_custom_catalog_add_sends_a_url_source_as_the_url_field(void)
{
    mock_response_t script[] = { { 200, "{\"status\":\"success\",\"result\":null}" } };
    serve(script, 1, 0);
    audd_client_t *c = mock_client(1);
    TEST_ASSERT_EQUAL_INT(AUDD_OK,
        audd_custom_catalog_add(c, 42, "https://example.com/song.mp3"));
    TEST_ASSERT_EQUAL_INT(1, mock_request_count(&g_srv));
    const char *r = g_srv.reqs[0];
    TEST_ASSERT_NOT_NULL(strstr(r, "POST /upload/ HTTP/1.1"));
    TEST_ASSERT_NOT_NULL(strstr(r, "name=\"url\"\r\n\r\nhttps://example.com/song.mp3\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(r, "name=\"audio_id\"\r\n\r\n42\r\n"));
    TEST_ASSERT_NULL(strstr(r, "filename="));
    audd_client_free(c);
    mock_server_stop(&g_srv);
}

void test_custom_catalog_add_is_sent_once(void)
{
    mock_response_t script[] = { { 503, "" } };
    serve(script, 1, 0);
    audd_client_t *c = mock_client(1);
    static const unsigned char bytes[64] = { 1, 2, 3 };
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_SERVER, audd_custom_catalog_add_bytes(c, 7, bytes, sizeof(bytes)));
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_SERVER,
        audd_custom_catalog_add(c, 7, "https://example.com/song.mp3"));
    TEST_ASSERT_EQUAL_INT(2, mock_request_count(&g_srv));
    audd_client_free(c);
    mock_server_stop(&g_srv);
}

/* ------------------------------------------------------------------ *
 * Stream error codes                                                  *
 * ------------------------------------------------------------------ */

void test_902_on_add_stream_is_the_stream_limit(void)
{
    char *body = read_fixture("error_902_stream_limit.json", NULL);
    TEST_ASSERT_NOT_NULL(body);
    mock_response_t script[] = { { 200, body } };
    serve(script, 1, 0);
    audd_client_t *c = mock_client(1);

    audd_add_stream_request_t req = { "https://example.com/radio.mp3", 999001, NULL, NULL };
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_STREAM_LIMIT, audd_streams_add(c, &req));
    TEST_ASSERT_EQUAL_INT(902, audd_last_error_code(c));

    /* Elsewhere 902 stays the request quota. */
    audd_stream_list_t *list = NULL;
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_QUOTA, audd_streams_list(c, &list));
    TEST_ASSERT_NULL(list);
    audd_client_free(c);
    mock_server_stop(&g_srv);
    free(body);
}

void test_no_callback_url_says_so(void)
{
    char *body = read_fixture("error_19_no_callback_url.json", NULL);
    TEST_ASSERT_NOT_NULL(body);
    mock_response_t script[] = { { 200, body } };
    serve(script, 1, 0);
    audd_client_t *c = mock_client(1);
    char *url = NULL;
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_BLOCKED, audd_streams_get_callback_url(c, &url));
    TEST_ASSERT_NULL(url);
    TEST_ASSERT_EQUAL_INT(19, audd_last_error_code(c));
    const char *msg = audd_last_error_message(c);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(msg, "no callback URL is configured"), msg);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(msg, "Getting the URL for callbacks failed"), msg);
    audd_client_free(c);
    mock_server_stop(&g_srv);
    free(body);
}

void test_callback_url_is_returned(void)
{
    mock_response_t script[] = {
        { 200, "{\"status\":\"success\",\"result\":\"https://example.com/hook\"}" }
    };
    serve(script, 1, 0);
    audd_client_t *c = mock_client(1);
    char *url = NULL;
    TEST_ASSERT_EQUAL_INT(AUDD_OK, audd_streams_get_callback_url(c, &url));
    TEST_ASSERT_EQUAL_STRING("https://example.com/hook", url);
    audd_string_free(url);
    audd_client_free(c);
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

    UNITY_BEGIN();
    RUN_TEST(test_closed_client_returns_closed_without_a_request);
    RUN_TEST(test_close_aborts_a_waiting_request);
    RUN_TEST(test_close_cuts_a_retry_pause_short);
    RUN_TEST(test_custom_catalog_add_sends_a_url_source_as_the_url_field);
    RUN_TEST(test_custom_catalog_add_is_sent_once);
    RUN_TEST(test_902_on_add_stream_is_the_stream_limit);
    RUN_TEST(test_no_callback_url_says_so);
    RUN_TEST(test_callback_url_is_returned);
    return UNITY_END();
}
