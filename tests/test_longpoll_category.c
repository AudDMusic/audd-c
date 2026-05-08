/* test_longpoll_category.c — DeriveLongpollCategory parity. */
#include "audd.h"
#include "unity.h"

#include <string.h>
#include <ctype.h>

void setUp(void) {}
void tearDown(void) {}

void test_stable_and_correct_length(void)
{
    char a[10], b[10], c[10];
    audd_error_t e1 = audd_derive_longpoll_category("test", 7, a);
    audd_error_t e2 = audd_derive_longpoll_category("test", 7, b);
    audd_error_t e3 = audd_derive_longpoll_category("test", 8, c);
    TEST_ASSERT_EQUAL(AUDD_OK, e1);
    TEST_ASSERT_EQUAL(AUDD_OK, e2);
    TEST_ASSERT_EQUAL(AUDD_OK, e3);
    TEST_ASSERT_EQUAL_INT(9, (int)strlen(a));
    TEST_ASSERT_EQUAL_STRING(a, b);
    TEST_ASSERT_NOT_EQUAL(0, strcmp(a, c));
}

void test_only_hex_chars(void)
{
    char cat[10];
    TEST_ASSERT_EQUAL(AUDD_OK, audd_derive_longpoll_category("test", 7, cat));
    for (int i = 0; i < 9; ++i) {
        TEST_ASSERT_TRUE_MESSAGE(
            (cat[i] >= '0' && cat[i] <= '9') || (cat[i] >= 'a' && cat[i] <= 'f'),
            "unexpected non-hex char in longpoll category");
    }
}

void test_different_tokens_different_outputs(void)
{
    char a[10], b[10];
    TEST_ASSERT_EQUAL(AUDD_OK, audd_derive_longpoll_category("test",  7, a));
    TEST_ASSERT_EQUAL(AUDD_OK, audd_derive_longpoll_category("other", 7, b));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(a, b));
}

/* The one-step entry point audd_longpoll_run_by_radio_id derives the
 * category internally from the client's stored api_token. Since
 * audd_longpoll_run actually performs HTTP, we can't unit-test the full
 * dispatch end-to-end here — but we can verify:
 *   (a) input validation shape matches audd_longpoll_run,
 *   (b) the category it would derive equals
 *       audd_derive_longpoll_category(token, radio_id) bit-for-bit,
 *       proving the by-radio-id form and the category-string form share
 *       the same wire identity.
 */
static void on_match_unused(const audd_stream_callback_match_t *m, void *ud)        { (void)m; (void)ud; }
static void on_notif_unused(const audd_stream_callback_notification_t *n, void *ud) { (void)n; (void)ud; }
static int g_err_fired;
static audd_error_t g_err_code;
static void on_err_capture(audd_error_t err, const char *msg, void *ud)
{
    (void)msg; (void)ud;
    g_err_fired = 1;
    g_err_code = err;
}

void test_run_by_radio_id_rejects_null_client(void)
{
    audd_longpoll_callbacks_t cb = { on_match_unused, on_notif_unused, on_err_capture, NULL };
    audd_longpoll_t *h = NULL;
    audd_error_t e = audd_longpoll_run_by_radio_id(NULL, 1, NULL, &cb, &h);
    TEST_ASSERT_EQUAL(AUDD_ERR_INVALID_ARGUMENT, e);
    TEST_ASSERT_NULL(h);
}

void test_run_by_radio_id_rejects_null_callbacks(void)
{
    audd_client_t *client = audd_client_new("test-token", NULL);
    TEST_ASSERT_NOT_NULL(client);
    audd_error_t e = audd_longpoll_run_by_radio_id(client, 1, NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(AUDD_ERR_INVALID_ARGUMENT, e);
    audd_client_free(client);
}

void test_run_by_radio_id_rejects_tokenless_client(void)
{
    audd_client_t *client = audd_client_new(NULL, NULL);
    TEST_ASSERT_NOT_NULL(client);
    audd_longpoll_callbacks_t cb = { on_match_unused, on_notif_unused, on_err_capture, NULL };
    audd_longpoll_t *h = NULL;
    audd_error_t e = audd_longpoll_run_by_radio_id(client, 1, NULL, &cb, &h);
    TEST_ASSERT_EQUAL(AUDD_ERR_AUTHENTICATION, e);
    TEST_ASSERT_NULL(h);
    audd_client_free(client);
}

void test_run_by_radio_id_derives_same_category_as_helper(void)
{
    /* Same token + radio_id must produce the same 9-char string the helper
     * yields. The new entry point uses audd_streams_derive_longpoll_category
     * internally, which delegates to audd_derive_longpoll_category, so the
     * category sent on the wire is identical to the category-string form. */
    const char *token = "test-token";
    audd_client_t *client = audd_client_new(token, NULL);
    TEST_ASSERT_NOT_NULL(client);

    char from_helper[10];
    char from_client[10];
    TEST_ASSERT_EQUAL(AUDD_OK, audd_derive_longpoll_category(token, 1, from_helper));
    TEST_ASSERT_EQUAL(AUDD_OK, audd_streams_derive_longpoll_category(client, 1, from_client));
    TEST_ASSERT_EQUAL_STRING(from_helper, from_client);

    /* Spot-check another radio_id. */
    char a[10], b[10];
    TEST_ASSERT_EQUAL(AUDD_OK, audd_derive_longpoll_category(token, 42, a));
    TEST_ASSERT_EQUAL(AUDD_OK, audd_streams_derive_longpoll_category(client, 42, b));
    TEST_ASSERT_EQUAL_STRING(a, b);

    audd_client_free(client);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_stable_and_correct_length);
    RUN_TEST(test_only_hex_chars);
    RUN_TEST(test_different_tokens_different_outputs);
    RUN_TEST(test_run_by_radio_id_rejects_null_client);
    RUN_TEST(test_run_by_radio_id_rejects_null_callbacks);
    RUN_TEST(test_run_by_radio_id_rejects_tokenless_client);
    RUN_TEST(test_run_by_radio_id_derives_same_category_as_helper);
    return UNITY_END();
}
