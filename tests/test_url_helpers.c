/* test_url_helpers.c — addReturnToURL parity tests against audd-go. */
#include "audd.h"
#include "audd_internal.h"
#include "unity.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

void test_empty_no_change(void)
{
    char *out = NULL;
    audd_error_t e = audd_url_append_return("https://x", NULL, &out);
    TEST_ASSERT_EQUAL(AUDD_OK, e);
    TEST_ASSERT_EQUAL_STRING("https://x", out);
    audd_free(out);
}

void test_appends_csv(void)
{
    const char *m[] = { "apple_music", "spotify", NULL };
    char *out = NULL;
    audd_error_t e = audd_url_append_return("https://x", m, &out);
    TEST_ASSERT_EQUAL(AUDD_OK, e);
    /* %2C is URL-encoded comma. */
    TEST_ASSERT_NOT_NULL(strstr(out, "return=apple_music%2Cspotify"));
    audd_free(out);
}

void test_preserves_existing_query(void)
{
    const char *m[] = { "deezer", NULL };
    char *out = NULL;
    audd_error_t e = audd_url_append_return("https://x?foo=1", m, &out);
    TEST_ASSERT_EQUAL(AUDD_OK, e);
    TEST_ASSERT_NOT_NULL(strstr(out, "foo=1"));
    TEST_ASSERT_NOT_NULL(strstr(out, "&return=deezer"));
    audd_free(out);
}

void test_duplicate_return_param_errs(void)
{
    const char *m[] = { "spotify", NULL };
    char *out = NULL;
    audd_error_t e = audd_url_append_return("https://x?return=apple_music", m, &out);
    TEST_ASSERT_EQUAL(AUDD_ERR_INVALID_ARGUMENT, e);
    TEST_ASSERT_NULL(out);
}

void test_hostname_match(void)
{
    TEST_ASSERT_TRUE(audd_url_hostname_is("https://lis.tn/abc", "lis.tn"));
    TEST_ASSERT_TRUE(audd_url_hostname_is("https://lis.tn:443/abc", "lis.tn"));
    TEST_ASSERT_TRUE(audd_url_hostname_is("https://lis.tn/abc?q=1", "lis.tn"));
    TEST_ASSERT_FALSE(audd_url_hostname_is("https://www.youtube.com/x", "lis.tn"));
    TEST_ASSERT_FALSE(audd_url_hostname_is("", "lis.tn"));
    TEST_ASSERT_FALSE(audd_url_hostname_is("not a url", "lis.tn"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_no_change);
    RUN_TEST(test_appends_csv);
    RUN_TEST(test_preserves_existing_query);
    RUN_TEST(test_duplicate_return_param_errs);
    RUN_TEST(test_hostname_match);
    return UNITY_END();
}
