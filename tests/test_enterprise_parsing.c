/* test_enterprise_parsing.c — flatten chunks + read ISRC/UPC + thumbnail. */
#include "audd.h"
#include "audd_internal.h"
#include "unity.h"

#include "../vendor/cJSON/cJSON.h"

void setUp(void) {}
void tearDown(void) {}

void test_flattens_chunks(void)
{
    const char *json = "["
        "{\"songs\":[{\"score\":100,\"artist\":\"A1\",\"title\":\"T1\",\"isrc\":\"US001\",\"upc\":\"000\"}],\"offset\":\"0:00\"},"
        "{\"songs\":[{\"score\":90,\"artist\":\"A2\",\"title\":\"T2\",\"start_offset\":10,\"end_offset\":30}],\"offset\":\"0:30\"}"
    "]";
    cJSON *obj = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(obj);
    audd_enterprise_result_t *r = audd_enterprise_from_json(obj);
    cJSON_Delete(obj);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_INT(2, (int)audd_enterprise_result_count(r));

    const audd_enterprise_match_t *m1 = audd_enterprise_result_at(r, 0);
    TEST_ASSERT_NOT_NULL(m1);
    TEST_ASSERT_EQUAL_INT(100, audd_enterprise_match_get_score(m1));
    TEST_ASSERT_EQUAL_STRING("A1", audd_enterprise_match_get_artist(m1));
    TEST_ASSERT_EQUAL_STRING("US001", audd_enterprise_match_get_isrc(m1));
    TEST_ASSERT_EQUAL_STRING("000", audd_enterprise_match_get_upc(m1));

    const audd_enterprise_match_t *m2 = audd_enterprise_result_at(r, 1);
    TEST_ASSERT_NOT_NULL(m2);
    TEST_ASSERT_EQUAL_INT(10, audd_enterprise_match_get_start_offset(m2));
    TEST_ASSERT_EQUAL_INT(30, audd_enterprise_match_get_end_offset(m2));

    audd_enterprise_result_free(r);
}

void test_thumbnail_url(void)
{
    const char *json = "[{\"songs\":[{\"score\":1,\"artist\":\"A\",\"title\":\"T\",\"song_link\":\"https://lis.tn/abc\"}],\"offset\":\"0:00\"}]";
    cJSON *obj = cJSON_Parse(json);
    audd_enterprise_result_t *r = audd_enterprise_from_json(obj);
    cJSON_Delete(obj);
    const audd_enterprise_match_t *m = audd_enterprise_result_at(r, 0);
    TEST_ASSERT_EQUAL_STRING("https://lis.tn/abc?thumb",
        audd_enterprise_match_thumbnail_url(m));
    TEST_ASSERT_EQUAL_STRING("https://lis.tn/abc?spotify",
        audd_enterprise_match_streaming_url(m, AUDD_PROVIDER_SPOTIFY));
    audd_enterprise_result_free(r);
}

void test_empty_result(void)
{
    cJSON *obj = cJSON_CreateArray();
    audd_enterprise_result_t *r = audd_enterprise_from_json(obj);
    cJSON_Delete(obj);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_INT(0, (int)audd_enterprise_result_count(r));
    TEST_ASSERT_NULL(audd_enterprise_result_at(r, 0));
    audd_enterprise_result_free(r);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_flattens_chunks);
    RUN_TEST(test_thumbnail_url);
    RUN_TEST(test_empty_result);
    return UNITY_END();
}
