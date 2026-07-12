/* test_longpoll_resilience.c — poll outcome classification + timeout coupling.
 *
 * The longpoll consumer must (a) reconnect on transient failures instead of
 * terminating, (b) treat auth/API errors as terminal, and (c) size the poll
 * socket timeout to outlast the server-side hold. */
#include "audd.h"
#include "audd_internal.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Connection/timeout-class failure (rc != 0) is transient → retry. */
void test_connection_failure_is_retry(void)
{
    TEST_ASSERT_EQUAL_INT(AUDD_LONGPOLL_RETRY,
        audd_longpoll_classify_http(-1, 0));
}

/* Transient HTTP statuses retry; other 4xx are terminal. */
void test_http_status_classification(void)
{
    TEST_ASSERT_EQUAL_INT(AUDD_LONGPOLL_RETRY,   audd_longpoll_classify_http(0, 408));
    TEST_ASSERT_EQUAL_INT(AUDD_LONGPOLL_RETRY,   audd_longpoll_classify_http(0, 429));
    TEST_ASSERT_EQUAL_INT(AUDD_LONGPOLL_RETRY,   audd_longpoll_classify_http(0, 500));
    TEST_ASSERT_EQUAL_INT(AUDD_LONGPOLL_RETRY,   audd_longpoll_classify_http(0, 503));
    TEST_ASSERT_EQUAL_INT(AUDD_LONGPOLL_TERMINAL, audd_longpoll_classify_http(0, 401));
    TEST_ASSERT_EQUAL_INT(AUDD_LONGPOLL_TERMINAL, audd_longpoll_classify_http(0, 403));
    TEST_ASSERT_EQUAL_INT(AUDD_LONGPOLL_TERMINAL, audd_longpoll_classify_http(0, 404));
}

/* A normal 2xx response is neither retry nor terminal — the caller inspects
 * the body. */
void test_ok_status_continues(void)
{
    TEST_ASSERT_EQUAL_INT(AUDD_LONGPOLL_CONTINUE, audd_longpoll_classify_http(0, 200));
}

/* Poll timeout must be at least the hold plus a margin, and never shorter than
 * the standard timeout. */
void test_poll_timeout_covers_hold(void)
{
    /* Hold (50s) + margin exceeds a small standard timeout → use hold+margin. */
    long t = audd_longpoll_poll_timeout(10, 50);
    TEST_ASSERT_TRUE_MESSAGE(t > 50, "poll timeout must outlast the hold");

    /* A large standard timeout wins over a short hold. */
    long t2 = audd_longpoll_poll_timeout(120, 5);
    TEST_ASSERT_EQUAL_INT(120, t2);

    /* Zero/negative standard timeout falls back to a sane default. */
    long t3 = audd_longpoll_poll_timeout(0, 50);
    TEST_ASSERT_TRUE(t3 > 50);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_connection_failure_is_retry);
    RUN_TEST(test_http_status_classification);
    RUN_TEST(test_ok_status_continues);
    RUN_TEST(test_poll_timeout_covers_hold);
    return UNITY_END();
}
