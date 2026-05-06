/* test_error_mapping.c — verify audd_sentinel_for_code matches the
 * server-side error catalog and audd-go's mapping. */
#include "audd.h"
#include "audd_internal.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void check(int code, audd_error_t expect)
{
    audd_error_t got = audd_sentinel_for_code(code);
    TEST_ASSERT_EQUAL_MESSAGE(expect, got, "code did not map to expected sentinel");
}

void test_authentication_codes(void)
{
    check(900, AUDD_ERR_AUTHENTICATION);
    check(901, AUDD_ERR_AUTHENTICATION);
    check(903, AUDD_ERR_AUTHENTICATION);
}
void test_quota(void) { check(902, AUDD_ERR_QUOTA); }
void test_subscription(void)
{
    check(904, AUDD_ERR_SUBSCRIPTION);
    check(905, AUDD_ERR_SUBSCRIPTION);
}
void test_invalid_request(void)
{
    check(50, AUDD_ERR_INVALID_REQUEST);
    check(51, AUDD_ERR_INVALID_REQUEST);
    check(600, AUDD_ERR_INVALID_REQUEST);
    check(602, AUDD_ERR_INVALID_REQUEST);
    check(701, AUDD_ERR_INVALID_REQUEST);
    check(906, AUDD_ERR_INVALID_REQUEST);
}
void test_invalid_audio(void)
{
    check(300, AUDD_ERR_INVALID_AUDIO);
    check(400, AUDD_ERR_INVALID_AUDIO);
    check(500, AUDD_ERR_INVALID_AUDIO);
}
void test_rate_and_stream(void)
{
    check(610, AUDD_ERR_STREAM_LIMIT);
    check(611, AUDD_ERR_RATE_LIMIT);
}
void test_blocked(void)
{
    check(19,    AUDD_ERR_BLOCKED);
    check(31337, AUDD_ERR_BLOCKED);
}
void test_other(void)
{
    check(20,  AUDD_ERR_NEEDS_UPDATE);
    check(907, AUDD_ERR_NOT_RELEASED);
    check(0,   AUDD_ERR_SERVER); /* fallback */
    check(999, AUDD_ERR_SERVER); /* fallback */
}
void test_error_strings_are_stable(void)
{
    TEST_ASSERT_NOT_NULL(audd_error_string(AUDD_OK));
    TEST_ASSERT_EQUAL_STRING("ok", audd_error_string(AUDD_OK));
    TEST_ASSERT_NOT_NULL(audd_error_string(AUDD_ERR_AUTHENTICATION));
    TEST_ASSERT_NOT_NULL(audd_error_string(AUDD_ERR_INVALID_AUDIO));
    TEST_ASSERT_NOT_NULL(audd_error_string(AUDD_ERR_RATE_LIMIT));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_authentication_codes);
    RUN_TEST(test_quota);
    RUN_TEST(test_subscription);
    RUN_TEST(test_invalid_request);
    RUN_TEST(test_invalid_audio);
    RUN_TEST(test_rate_and_stream);
    RUN_TEST(test_blocked);
    RUN_TEST(test_other);
    RUN_TEST(test_error_strings_are_stable);
    return UNITY_END();
}
