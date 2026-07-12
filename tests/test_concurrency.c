/* test_concurrency.c — concurrent token rotation + last-error access.
 *
 * The client guards its api_token and last-error/last-request-id state with a
 * mutex, so hammering it from several threads must be data-race free. Run this
 * under ThreadSanitizer (or ASan) — it should report nothing. */
#include "audd.h"
#include "audd_internal.h"
#include "unity.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

#define ITERS 20000

static audd_client_t *g_client;

static void *rotate_thread(void *arg)
{
    (void)arg;
    for (int i = 0; i < ITERS; ++i) {
        char tok[32];
        snprintf(tok, sizeof(tok), "token-%d", i & 0xff);
        audd_client_set_api_token(g_client, tok);
    }
    return NULL;
}

static void *set_error_thread(void *arg)
{
    (void)arg;
    for (int i = 0; i < ITERS; ++i) {
        audd_client_set_error(g_client, "transient error", i % 1000);
        audd_client_set_request_id(g_client, "req-abc");
    }
    return NULL;
}

static void *read_thread(void *arg)
{
    (void)arg;
    volatile size_t sink = 0;
    for (int i = 0; i < ITERS; ++i) {
        /* Calling the accessors is data-race free. The error-message /
         * request-id accessors return a BORROWED pointer that a concurrent
         * writer may free (documented lifetime), so we don't dereference those
         * here. The error code is returned by value and the token accessor
         * returns an owned copy — both safe to use across concurrent writes. */
        sink += (size_t)audd_last_error_code(g_client);
        (void)audd_last_error_message(g_client);
        (void)audd_last_request_id(g_client);
        char *tok = audd_client_copy_api_token(g_client);
        if (tok) { sink += strlen(tok); audd_string_free(tok); }
    }
    (void)sink;
    return NULL;
}

void test_concurrent_rotate_and_error_access(void)
{
    g_client = audd_client_new("initial-token", NULL);
    TEST_ASSERT_NOT_NULL(g_client);

    pthread_t t[6];
    pthread_create(&t[0], NULL, rotate_thread, NULL);
    pthread_create(&t[1], NULL, rotate_thread, NULL);
    pthread_create(&t[2], NULL, set_error_thread, NULL);
    pthread_create(&t[3], NULL, set_error_thread, NULL);
    pthread_create(&t[4], NULL, read_thread, NULL);
    pthread_create(&t[5], NULL, read_thread, NULL);
    for (int i = 0; i < 6; ++i) pthread_join(t[i], NULL);

    audd_client_free(g_client);
    TEST_PASS();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_concurrent_rotate_and_error_access);
    return UNITY_END();
}
