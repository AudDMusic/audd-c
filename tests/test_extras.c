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

void test_stream_callback_song_extras_and_audio_id(void)
{
    const char *body = "{\"status\":\"success\",\"result\":{\"radio_id\":1,\"results\":["
        "{\"artist\":\"A\",\"title\":\"T\",\"score\":\"85\","
        "\"apple_music\":{\"url\":\"https://music.apple.com/x\"},\"new_field\":7},"
        "{\"audio_id\":\"146\",\"score\":100}]}}";
    audd_stream_callback_match_t *m = NULL;
    audd_stream_callback_notification_t *n = NULL;
    TEST_ASSERT_EQUAL_INT(AUDD_OK, audd_parse_callback(body, strlen(body), &m, &n, NULL));
    TEST_ASSERT_NOT_NULL(m);

    const audd_stream_callback_song_t *s = audd_stream_callback_match_get_song(m);
    TEST_ASSERT_EQUAL_INT(85, audd_stream_callback_song_get_score(s));
    TEST_ASSERT_EQUAL_INT(0, audd_stream_callback_song_has_audio_id(s));
    const char *am = audd_stream_callback_song_extra(s, "apple_music");
    TEST_ASSERT_EQUAL_STRING("{\"url\":\"https://music.apple.com/x\"}", am);
    TEST_ASSERT_TRUE(am == audd_stream_callback_song_extra(s, "apple_music")); /* cached */
    TEST_ASSERT_EQUAL_STRING("7", audd_stream_callback_song_extra(s, "new_field"));
    TEST_ASSERT_NULL(audd_stream_callback_song_extra(s, "artist")); /* typed */
    TEST_ASSERT_NULL(audd_stream_callback_song_extra(s, "missing"));
    TEST_ASSERT_NULL(audd_stream_callback_song_extra(s, NULL));
    TEST_ASSERT_NULL(audd_stream_callback_song_extra(NULL, "apple_music"));

    /* A custom-catalog match carries only audio_id. */
    const audd_stream_callback_song_t *alt = audd_stream_callback_match_get_alternative(m, 0);
    TEST_ASSERT_EQUAL_INT(1, audd_stream_callback_song_has_audio_id(alt));
    TEST_ASSERT_EQUAL_INT(146, audd_stream_callback_song_get_audio_id(alt));
    TEST_ASSERT_NULL(audd_stream_callback_song_get_artist(alt));
    TEST_ASSERT_EQUAL_INT(0, audd_stream_callback_song_has_audio_id(NULL));
    audd_stream_callback_match_free(m);
}

void test_enterprise_match_extras(void)
{
    const char *json = "[{\"songs\":[{\"score\":\"90\",\"artist\":\"A\","
        "\"apple_music\":{\"url\":\"x\"},\"new_field\":[1,2]}],\"offset\":\"00:00\"}]";
    cJSON *obj = cJSON_Parse(json);
    audd_enterprise_result_t *r = audd_enterprise_from_json(obj);
    cJSON_Delete(obj);
    TEST_ASSERT_NOT_NULL(r);
    const audd_enterprise_match_t *m = audd_enterprise_result_at(r, 0);
    TEST_ASSERT_EQUAL_INT(90, audd_enterprise_match_get_score(m));
    TEST_ASSERT_EQUAL_STRING("{\"url\":\"x\"}", audd_enterprise_match_extra(m, "apple_music"));
    TEST_ASSERT_EQUAL_STRING("[1,2]", audd_enterprise_match_extra(m, "new_field"));
    TEST_ASSERT_NULL(audd_enterprise_match_extra(m, "score")); /* typed */
    TEST_ASSERT_NULL(audd_enterprise_match_extra(m, "nope"));
    TEST_ASSERT_NULL(audd_enterprise_match_extra(m, NULL));
    TEST_ASSERT_NULL(audd_enterprise_match_extra(NULL, "x"));
    audd_enterprise_result_free(r);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_unknown_keys_visible_via_extra);
    RUN_TEST(test_apple_music_extras);
    RUN_TEST(test_preview_url_priority);
    RUN_TEST(test_stream_callback_song_extras_and_audio_id);
    RUN_TEST(test_enterprise_match_extras);
    return UNITY_END();
}
