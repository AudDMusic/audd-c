/* test_extras.c — verify extras-by-key access for unknown server fields. */
#include "audd.h"
#include "audd_internal.h"
#include "unity.h"

#include "../vendor/cJSON/cJSON.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

void test_unknown_keys_visible_via_extra(void)
{
    const char *json = "{"
        "\"timecode\":\"00:56\","
        "\"artist\":\"X\","
        "\"title\":\"Y\","
        "\"new_field_2027\":\"future-proof\","
        "\"extra_block\":{\"a\":1}"
    "}";
    cJSON *obj = cJSON_Parse(json);
    audd_recognition_t *r = audd_recognition_from_json(obj);
    cJSON_Delete(obj);
    TEST_ASSERT_NOT_NULL(r);
    /* New unknown string key — printed JSON includes the surrounding quotes */
    const char *e1 = audd_recognition_extra(r, "new_field_2027");
    TEST_ASSERT_NOT_NULL(e1);
    TEST_ASSERT_NOT_NULL(strstr(e1, "future-proof"));
    /* Unknown object key */
    const char *e2 = audd_recognition_extra(r, "extra_block");
    TEST_ASSERT_NOT_NULL(e2);
    TEST_ASSERT_NOT_NULL(strstr(e2, "\"a\""));
    /* Known key — explicitly NOT in extras */
    const char *e3 = audd_recognition_extra(r, "timecode");
    TEST_ASSERT_NULL(e3);
    audd_recognition_free(r);
}

void test_apple_music_extras(void)
{
    const char *json = "{"
        "\"timecode\":\"00:00\","
        "\"apple_music\":{"
            "\"artistName\":\"X\","
            "\"name\":\"T\","
            "\"previews\":[{\"url\":\"https://x\"}],"
            "\"artwork\":{\"url\":\"https://y\"}"
        "}"
    "}";
    cJSON *obj = cJSON_Parse(json);
    audd_recognition_t *r = audd_recognition_from_json(obj);
    cJSON_Delete(obj);
    const audd_apple_music_t *am = audd_recognition_apple_music(r);
    TEST_ASSERT_NOT_NULL(am);
    TEST_ASSERT_EQUAL_STRING("X", audd_apple_music_get_artist_name(am));
    /* The "artwork" field isn't a typed property → reachable via extras. */
    const char *art = audd_apple_music_extra(am, "artwork");
    TEST_ASSERT_NOT_NULL(art);
    TEST_ASSERT_NOT_NULL(strstr(art, "https://y"));
    audd_recognition_free(r);
}

void test_preview_url_priority(void)
{
    const char *json = "{"
        "\"apple_music\":{\"previews\":[{\"url\":\"apple://p\"}]},"
        "\"spotify\":{\"preview_url\":\"spotify://p\"},"
        "\"deezer\":{\"preview\":\"deezer://p\"}"
    "}";
    cJSON *obj = cJSON_Parse(json);
    audd_recognition_t *r = audd_recognition_from_json(obj);
    cJSON_Delete(obj);
    /* Apple Music wins. */
    TEST_ASSERT_EQUAL_STRING("apple://p", audd_recognition_preview_url(r));
    audd_recognition_free(r);

    /* Without Apple, Spotify wins. */
    obj = cJSON_Parse("{\"spotify\":{\"preview_url\":\"spotify://p\"},\"deezer\":{\"preview\":\"deezer://p\"}}");
    r = audd_recognition_from_json(obj);
    cJSON_Delete(obj);
    TEST_ASSERT_EQUAL_STRING("spotify://p", audd_recognition_preview_url(r));
    audd_recognition_free(r);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_unknown_keys_visible_via_extra);
    RUN_TEST(test_apple_music_extras);
    RUN_TEST(test_preview_url_priority);
    return UNITY_END();
}
