/* test_callback_parsing.c — parity with audd-go ParseCallback tests. */
#include "audd.h"
#include "unity.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void parse(const char *body, audd_stream_callback_match_t **m,
                  audd_stream_callback_notification_t **n, char **err)
{
    audd_error_t e = audd_parse_callback(body, strlen(body), m, n, err);
    TEST_ASSERT_EQUAL(AUDD_OK, e);
}

void test_parse_match(void)
{
    const char *body = "{\"status\":\"success\",\"result\":"
                       "{\"radio_id\":7,\"timestamp\":\"2020-04-13 10:31:43\","
                       "\"play_length\":111,"
                       "\"results\":[{\"artist\":\"A\",\"title\":\"T\",\"score\":100}]}}";
    audd_stream_callback_match_t *m = NULL;
    audd_stream_callback_notification_t *n = NULL;
    char *err = NULL;
    parse(body, &m, &n, &err);
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_NULL(n);
    TEST_ASSERT_EQUAL_INT64(7, audd_stream_callback_match_get_radio_id(m));
    TEST_ASSERT_EQUAL_INT(111, audd_stream_callback_match_get_play_length(m));
    const audd_stream_callback_song_t *song = audd_stream_callback_match_get_song(m);
    TEST_ASSERT_EQUAL_STRING("A", audd_stream_callback_song_get_artist(song));
    TEST_ASSERT_EQUAL_STRING("T", audd_stream_callback_song_get_title(song));
    TEST_ASSERT_EQUAL_INT(100, audd_stream_callback_song_get_score(song));
    TEST_ASSERT_EQUAL_INT(0, (int)audd_stream_callback_match_alternatives_count(m));
    audd_stream_callback_match_free(m);
}

void test_parse_match_with_alternatives(void)
{
    const char *body = "{\"status\":\"success\",\"result\":"
                       "{\"radio_id\":7,\"timestamp\":\"x\","
                       "\"results\":[{\"artist\":\"A\",\"title\":\"T\",\"score\":100},"
                                    "{\"artist\":\"A2\",\"title\":\"T2\",\"score\":80}]}}";
    audd_stream_callback_match_t *m = NULL;
    audd_stream_callback_notification_t *n = NULL;
    char *err = NULL;
    parse(body, &m, &n, &err);
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL_STRING("A", audd_stream_callback_song_get_artist(audd_stream_callback_match_get_song(m)));
    TEST_ASSERT_EQUAL_INT(1, (int)audd_stream_callback_match_alternatives_count(m));
    const audd_stream_callback_song_t *alt = audd_stream_callback_match_get_alternative(m, 0);
    TEST_ASSERT_EQUAL_STRING("A2", audd_stream_callback_song_get_artist(alt));
    audd_stream_callback_match_free(m);
}

void test_parse_notification(void)
{
    const char *body = "{\"status\":\"-\",\"notification\":"
                       "{\"radio_id\":3,\"stream_running\":false,"
                       "\"notification_code\":650,\"notification_message\":\"can't connect\"},"
                       "\"time\":1587939136}";
    audd_stream_callback_match_t *m = NULL;
    audd_stream_callback_notification_t *n = NULL;
    char *err = NULL;
    parse(body, &m, &n, &err);
    TEST_ASSERT_NULL(m);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_EQUAL_INT(3, audd_stream_callback_notification_get_radio_id(n));
    TEST_ASSERT_EQUAL_INT(0, audd_stream_callback_notification_get_stream_running(n));
    TEST_ASSERT_EQUAL_INT(650, audd_stream_callback_notification_get_code(n));
    TEST_ASSERT_EQUAL_INT(1587939136, audd_stream_callback_notification_get_time(n));
    audd_stream_callback_notification_free(n);
}

void test_bad_json(void)
{
    audd_stream_callback_match_t *m = NULL;
    audd_stream_callback_notification_t *n = NULL;
    char *err = NULL;
    audd_error_t e = audd_parse_callback("not json", 8, &m, &n, &err);
    TEST_ASSERT_NOT_EQUAL(AUDD_OK, e);
    TEST_ASSERT_NULL(m);
    TEST_ASSERT_NULL(n);
    audd_string_free(err);
}

void test_neither_result_nor_notification(void)
{
    audd_stream_callback_match_t *m = NULL;
    audd_stream_callback_notification_t *n = NULL;
    char *err = NULL;
    audd_error_t e = audd_parse_callback("{\"foo\":\"bar\"}", 13, &m, &n, &err);
    TEST_ASSERT_NOT_EQUAL(AUDD_OK, e);
    TEST_ASSERT_NULL(m);
    TEST_ASSERT_NULL(n);
    TEST_ASSERT_NOT_NULL(err);
    TEST_ASSERT_NOT_NULL(strstr(err, "neither"));
    audd_string_free(err);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_match);
    RUN_TEST(test_parse_match_with_alternatives);
    RUN_TEST(test_parse_notification);
    RUN_TEST(test_bad_json);
    RUN_TEST(test_neither_result_nor_notification);
    return UNITY_END();
}
