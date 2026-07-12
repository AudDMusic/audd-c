/* test_scalar_coercion.c — lenient scalar coercion in the shared JSON getters.
 *
 * Wrong-typed-but-convertible scalar fields are coerced; non-convertible
 * values and wrong-shaped containers degrade to the caller's default. Coerced
 * strings carry the same ownership/free semantics as native string fields. */
#include "audd.h"
#include "audd_internal.h"
#include "unity.h"

#include "../vendor/cJSON/cJSON.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ---------------- expect STRING ---------------- */

void test_string_from_int(void)
{
    cJSON *o = cJSON_Parse("{\"artist\":123}");
    TEST_ASSERT_EQUAL_STRING("123", audd_json_get_string(o, "artist"));
    cJSON_Delete(o);
}

void test_string_from_double(void)
{
    cJSON *o = cJSON_Parse("{\"x\":8.5}");
    TEST_ASSERT_EQUAL_STRING("8.5", audd_json_get_string(o, "x"));
    cJSON_Delete(o);
}

void test_string_from_bool(void)
{
    cJSON *o = cJSON_Parse("{\"t\":true,\"f\":false}");
    TEST_ASSERT_EQUAL_STRING("true", audd_json_get_string(o, "t"));
    TEST_ASSERT_EQUAL_STRING("false", audd_json_get_string(o, "f"));
    cJSON_Delete(o);
}

void test_string_from_container_degrades(void)
{
    cJSON *o = cJSON_Parse("{\"a\":[1,2],\"b\":{\"k\":1},\"n\":null}");
    TEST_ASSERT_NULL(audd_json_get_string(o, "a"));
    TEST_ASSERT_NULL(audd_json_get_string(o, "b"));
    TEST_ASSERT_NULL(audd_json_get_string(o, "n"));
    TEST_ASSERT_NULL(audd_json_get_string(o, "missing"));
    cJSON_Delete(o);
}

void test_string_fast_path_unaffected(void)
{
    cJSON *o = cJSON_Parse("{\"s\":\"hello\"}");
    TEST_ASSERT_EQUAL_STRING("hello", audd_json_get_string(o, "s"));
    cJSON_Delete(o);
}

/* ---------------- expect INT ---------------- */

void test_int_from_numeric_string(void)
{
    cJSON *o = cJSON_Parse("{\"score\":\"85\"}");
    TEST_ASSERT_EQUAL_INT(85, audd_json_get_int(o, "score", -1));
    cJSON_Delete(o);
}

void test_int_from_fractional_string_truncates(void)
{
    cJSON *o = cJSON_Parse("{\"score\":\"8.9\"}");
    TEST_ASSERT_EQUAL_INT(8, audd_json_get_int(o, "score", -1));
    cJSON_Delete(o);
}

void test_int_from_double_truncates_toward_zero(void)
{
    cJSON *o = cJSON_Parse("{\"a\":8.9,\"b\":-8.9}");
    TEST_ASSERT_EQUAL_INT(8, audd_json_get_int(o, "a", -1));
    TEST_ASSERT_EQUAL_INT(-8, audd_json_get_int(o, "b", 999));
    cJSON_Delete(o);
}

void test_int_from_bool(void)
{
    cJSON *o = cJSON_Parse("{\"t\":true,\"f\":false}");
    TEST_ASSERT_EQUAL_INT(1, audd_json_get_int(o, "t", -1));
    TEST_ASSERT_EQUAL_INT(0, audd_json_get_int(o, "f", -1));
    cJSON_Delete(o);
}

void test_int_junk_string_degrades(void)
{
    cJSON *o = cJSON_Parse("{\"a\":\"abc\",\"b\":\"12abc\",\"c\":\"NaN\","
                           "\"d\":\"Infinity\",\"e\":\"0x1A\",\"f\":\"inf\"}");
    TEST_ASSERT_EQUAL_INT(-1, audd_json_get_int(o, "a", -1));
    TEST_ASSERT_EQUAL_INT(-1, audd_json_get_int(o, "b", -1)); /* no partial parse */
    TEST_ASSERT_EQUAL_INT(-1, audd_json_get_int(o, "c", -1));
    TEST_ASSERT_EQUAL_INT(-1, audd_json_get_int(o, "d", -1));
    TEST_ASSERT_EQUAL_INT(-1, audd_json_get_int(o, "e", -1));
    TEST_ASSERT_EQUAL_INT(-1, audd_json_get_int(o, "f", -1));
    cJSON_Delete(o);
}

void test_int_whitespace_trimmed(void)
{
    cJSON *o = cJSON_Parse("{\"a\":\" 85 \"}");
    TEST_ASSERT_EQUAL_INT(85, audd_json_get_int(o, "a", -1));
    cJSON_Delete(o);
}

void test_int_container_degrades(void)
{
    cJSON *o = cJSON_Parse("{\"a\":[1],\"b\":{\"k\":1}}");
    TEST_ASSERT_EQUAL_INT(7, audd_json_get_int(o, "a", 7));
    TEST_ASSERT_EQUAL_INT(7, audd_json_get_int(o, "b", 7));
    cJSON_Delete(o);
}

void test_int64_numeric_string(void)
{
    cJSON *o = cJSON_Parse("{\"big\":\"9007199254740993\"}");
    TEST_ASSERT_EQUAL_INT64((int64_t)9007199254740993LL,
                            audd_json_get_int64(o, "big", 0));
    cJSON_Delete(o);
}

/* ---------------- expect DOUBLE ---------------- */

void test_double_from_int(void)
{
    cJSON *o = cJSON_Parse("{\"x\":8}");
    /* Unity double asserts disabled in this build: compare in tenths. */
    TEST_ASSERT_EQUAL_INT(80, (int)(audd_json_get_double(o, "x", -1.0) * 10.0 + 0.5));
    cJSON_Delete(o);
}

void test_double_from_numeric_string(void)
{
    cJSON *o = cJSON_Parse("{\"x\":\" 8.5 \"}");
    TEST_ASSERT_EQUAL_INT(85, (int)(audd_json_get_double(o, "x", -1.0) * 10.0 + 0.5));
    cJSON_Delete(o);
}

void test_double_junk_degrades(void)
{
    cJSON *o = cJSON_Parse("{\"a\":\"NaN\",\"b\":\"Infinity\",\"c\":\"0x1A\","
                           "\"d\":\"8.5x\"}");
    TEST_ASSERT_EQUAL_INT(-10, (int)(audd_json_get_double(o, "a", -1.0) * 10.0));
    TEST_ASSERT_EQUAL_INT(-10, (int)(audd_json_get_double(o, "b", -1.0) * 10.0));
    TEST_ASSERT_EQUAL_INT(-10, (int)(audd_json_get_double(o, "c", -1.0) * 10.0));
    TEST_ASSERT_EQUAL_INT(-10, (int)(audd_json_get_double(o, "d", -1.0) * 10.0));
    cJSON_Delete(o);
}

/* ---------------- expect BOOL (strict whitelist, both directions) ---------- */

void test_bool_number(void)
{
    cJSON *o = cJSON_Parse("{\"one\":1,\"zero\":0,\"two_half\":2.5}");
    TEST_ASSERT_EQUAL_INT(1, audd_json_get_bool(o, "one", -1));
    TEST_ASSERT_EQUAL_INT(0, audd_json_get_bool(o, "zero", -1));
    TEST_ASSERT_EQUAL_INT(1, audd_json_get_bool(o, "two_half", -1)); /* !=0 */
    cJSON_Delete(o);
}

void test_bool_string_truthy_whitelist(void)
{
    cJSON *o = cJSON_Parse("{\"a\":\"true\",\"b\":\"1\",\"c\":\"yes\",\"d\":\"on\","
                           "\"e\":\"TRUE\",\"f\":\" on \",\"g\":\"YES\"}");
    TEST_ASSERT_EQUAL_INT(1, audd_json_get_bool(o, "a", -1));
    TEST_ASSERT_EQUAL_INT(1, audd_json_get_bool(o, "b", -1));
    TEST_ASSERT_EQUAL_INT(1, audd_json_get_bool(o, "c", -1));
    TEST_ASSERT_EQUAL_INT(1, audd_json_get_bool(o, "d", -1));
    TEST_ASSERT_EQUAL_INT(1, audd_json_get_bool(o, "e", -1)); /* case-insensitive */
    TEST_ASSERT_EQUAL_INT(1, audd_json_get_bool(o, "f", -1)); /* trimmed */
    TEST_ASSERT_EQUAL_INT(1, audd_json_get_bool(o, "g", -1));
    cJSON_Delete(o);
}

void test_bool_string_falsey_whitelist(void)
{
    cJSON *o = cJSON_Parse("{\"a\":\"false\",\"b\":\"0\",\"c\":\"no\",\"d\":\"off\","
                           "\"e\":\"FALSE\",\"f\":\" No \",\"g\":\"\"}");
    TEST_ASSERT_EQUAL_INT(0, audd_json_get_bool(o, "a", -1));
    TEST_ASSERT_EQUAL_INT(0, audd_json_get_bool(o, "b", -1));
    TEST_ASSERT_EQUAL_INT(0, audd_json_get_bool(o, "c", -1));
    TEST_ASSERT_EQUAL_INT(0, audd_json_get_bool(o, "d", -1));
    TEST_ASSERT_EQUAL_INT(0, audd_json_get_bool(o, "e", -1)); /* case-insensitive */
    TEST_ASSERT_EQUAL_INT(0, audd_json_get_bool(o, "f", -1)); /* trimmed */
    TEST_ASSERT_EQUAL_INT(0, audd_json_get_bool(o, "g", -1)); /* empty string */
    cJSON_Delete(o);
}

void test_bool_string_unrecognized_degrades(void)
{
    cJSON *o = cJSON_Parse("{\"a\":\"maybe\",\"b\":\"2\",\"c\":\"enabled\"}");
    /* Unrecognized tokens degrade to default — NOT treated as true. */
    TEST_ASSERT_EQUAL_INT(-1, audd_json_get_bool(o, "a", -1));
    TEST_ASSERT_EQUAL_INT(-1, audd_json_get_bool(o, "b", -1));
    TEST_ASSERT_EQUAL_INT(-1, audd_json_get_bool(o, "c", -1));
    cJSON_Delete(o);
}

void test_bool_container_degrades(void)
{
    cJSON *o = cJSON_Parse("{\"a\":[true],\"b\":{\"k\":true}}");
    TEST_ASSERT_EQUAL_INT(-1, audd_json_get_bool(o, "a", -1));
    TEST_ASSERT_EQUAL_INT(-1, audd_json_get_bool(o, "b", -1));
    cJSON_Delete(o);
}

/* ---------------- through the model parsers ---------------- */

void test_recognition_numeric_artist_renders_string(void)
{
    /* Wrong-typed artist arrives as a number; the typed getter renders it,
     * but raw_response() preserves the original wire type. */
    cJSON *obj = cJSON_Parse("{\"artist\":123,\"title\":\"T\"}");
    audd_recognition_t *r = audd_recognition_from_json(obj);
    cJSON_Delete(obj);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_STRING("123", audd_recognition_get_artist(r));
    TEST_ASSERT_EQUAL_STRING("T", audd_recognition_get_title(r));
    /* raw keeps the number as sent, un-coerced. */
    const char *raw = audd_recognition_raw_response(r);
    TEST_ASSERT_NOT_NULL(raw);
    TEST_ASSERT_NOT_NULL(strstr(raw, "\"artist\":123"));
    audd_recognition_free(r);
}

void test_enterprise_string_score_and_offsets(void)
{
    /* Enterprise offsets and score arriving as numeric strings must parse. */
    const char *json = "[{\"offset\":\"0:00\",\"songs\":[{"
        "\"score\":\"85\",\"artist\":\"A\",\"title\":\"T\","
        "\"start_offset\":\"1000\",\"end_offset\":\"2000\"}]}]";
    cJSON *obj = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(obj);
    audd_enterprise_result_t *r = audd_enterprise_from_json(obj);
    cJSON_Delete(obj);
    TEST_ASSERT_NOT_NULL(r);
    const audd_enterprise_match_t *m = audd_enterprise_result_at(r, 0);
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL_INT(85, audd_enterprise_match_get_score(m));
    TEST_ASSERT_EQUAL_INT(1000, audd_enterprise_match_get_start_offset(m));
    TEST_ASSERT_EQUAL_INT(2000, audd_enterprise_match_get_end_offset(m));
    audd_enterprise_result_free(r);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_string_from_int);
    RUN_TEST(test_string_from_double);
    RUN_TEST(test_string_from_bool);
    RUN_TEST(test_string_from_container_degrades);
    RUN_TEST(test_string_fast_path_unaffected);

    RUN_TEST(test_int_from_numeric_string);
    RUN_TEST(test_int_from_fractional_string_truncates);
    RUN_TEST(test_int_from_double_truncates_toward_zero);
    RUN_TEST(test_int_from_bool);
    RUN_TEST(test_int_junk_string_degrades);
    RUN_TEST(test_int_whitespace_trimmed);
    RUN_TEST(test_int_container_degrades);
    RUN_TEST(test_int64_numeric_string);

    RUN_TEST(test_double_from_int);
    RUN_TEST(test_double_from_numeric_string);
    RUN_TEST(test_double_junk_degrades);

    RUN_TEST(test_bool_number);
    RUN_TEST(test_bool_string_truthy_whitelist);
    RUN_TEST(test_bool_string_falsey_whitelist);
    RUN_TEST(test_bool_string_unrecognized_degrades);
    RUN_TEST(test_bool_container_degrades);

    RUN_TEST(test_recognition_numeric_artist_renders_string);
    RUN_TEST(test_enterprise_string_score_and_offsets);
    return UNITY_END();
}
