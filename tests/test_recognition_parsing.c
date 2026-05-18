/* test_recognition_parsing.c — parse Recognition / public + custom matches. */
#include "audd.h"
#include "audd_internal.h"
#include "unity.h"

#include "../vendor/cJSON/cJSON.h"

#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static audd_recognition_t *parse_recognition(const char *json)
{
    cJSON *obj = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(obj);
    audd_recognition_t *r = audd_recognition_from_json(obj);
    cJSON_Delete(obj);
    return r;
}

void test_public_match(void)
{
    const char *json = "{"
        "\"timecode\":\"00:56\","
        "\"artist\":\"Tears For Fears\","
        "\"title\":\"Everybody Wants To Rule The World\","
        "\"song_link\":\"https://lis.tn/NbkVb\""
    "}";
    audd_recognition_t *r = parse_recognition(json);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_STRING("Tears For Fears", audd_recognition_get_artist(r));
    TEST_ASSERT_EQUAL_STRING("Everybody Wants To Rule The World", audd_recognition_get_title(r));
    TEST_ASSERT_TRUE(audd_recognition_is_public_match(r));
    TEST_ASSERT_FALSE(audd_recognition_is_custom_match(r));
    audd_recognition_free(r);
}

void test_custom_match(void)
{
    const char *json = "{\"timecode\":\"01:45\",\"audio_id\":146}";
    audd_recognition_t *r = parse_recognition(json);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_TRUE(audd_recognition_is_custom_match(r));
    TEST_ASSERT_FALSE(audd_recognition_is_public_match(r));
    TEST_ASSERT_TRUE(audd_recognition_has_audio_id(r));
    TEST_ASSERT_EQUAL_INT(146, audd_recognition_get_audio_id(r));
    audd_recognition_free(r);
}

void test_thumbnail_url(void)
{
    audd_recognition_t *r = parse_recognition("{\"song_link\":\"https://lis.tn/NbkVb\"}");
    TEST_ASSERT_EQUAL_STRING("https://lis.tn/NbkVb?thumb", audd_recognition_thumbnail_url(r));
    audd_recognition_free(r);

    r = parse_recognition("{\"song_link\":\"https://lis.tn/x?foo=1\"}");
    TEST_ASSERT_EQUAL_STRING("https://lis.tn/x?foo=1&thumb", audd_recognition_thumbnail_url(r));
    audd_recognition_free(r);

    r = parse_recognition("{\"song_link\":\"https://www.youtube.com/watch?v=abc\"}");
    TEST_ASSERT_NULL(audd_recognition_thumbnail_url(r));
    audd_recognition_free(r);

    r = parse_recognition("{\"song_link\":\"\"}");
    TEST_ASSERT_NULL(audd_recognition_thumbnail_url(r));
    audd_recognition_free(r);
}

void test_streaming_url_lis_tn_redirect(void)
{
    audd_recognition_t *r = parse_recognition("{\"song_link\":\"https://lis.tn/abc\"}");
    TEST_ASSERT_EQUAL_STRING("https://lis.tn/abc?spotify",
        audd_recognition_streaming_url(r, AUDD_PROVIDER_SPOTIFY));
    TEST_ASSERT_EQUAL_STRING("https://lis.tn/abc?apple_music",
        audd_recognition_streaming_url(r, AUDD_PROVIDER_APPLE_MUSIC));
    TEST_ASSERT_EQUAL_STRING("https://lis.tn/abc?youtube",
        audd_recognition_streaming_url(r, AUDD_PROVIDER_YOUTUBE));
    audd_recognition_free(r);
}

void test_streaming_url_no_song_link(void)
{
    audd_recognition_t *r = parse_recognition("{\"artist\":\"X\"}");
    TEST_ASSERT_NULL(audd_recognition_streaming_url(r, AUDD_PROVIDER_SPOTIFY));
    audd_recognition_free(r);
}

void test_isrc_upc(void)
{
    const char *json = "{\"isrc\":\"US001\",\"upc\":\"00000000\"}";
    audd_recognition_t *r = parse_recognition(json);
    TEST_ASSERT_EQUAL_STRING("US001", audd_recognition_get_isrc(r));
    TEST_ASSERT_EQUAL_STRING("00000000", audd_recognition_get_upc(r));
    audd_recognition_free(r);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_public_match);
    RUN_TEST(test_custom_match);
    RUN_TEST(test_thumbnail_url);
    RUN_TEST(test_streaming_url_lis_tn_redirect);
    RUN_TEST(test_streaming_url_no_song_link);
    RUN_TEST(test_isrc_upc);
    return UNITY_END();
}
