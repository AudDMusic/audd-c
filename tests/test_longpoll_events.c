/* test_longpoll_events.c — how the longpoll consumer reads one response body.
 *
 * Drives audd_longpoll_process_body directly with the shared fixtures
 * (tests/fixtures/longpoll_*.json) and with malformed variants: missing
 * fields, wrong types, `data` sent as a JSON string, empty lists, and an
 * allocator that fails at every allocation in turn. Run under ASan/UBSan
 * (and LeakSanitizer) these also check for leaks and out-of-bounds reads. */
#include "audd.h"
#include "audd_internal.h"
#include "unity.h"
#include "test_fixtures.h"

#include "../vendor/cJSON/cJSON.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 * Recording callbacks                                                 *
 * ------------------------------------------------------------------ */

#define MAX_SEEN 8

typedef struct {
    int       matches;
    int       notifications;
    int       errors;
    char      artist[MAX_SEEN][128];
    char      title[MAX_SEEN][128];
    long long radio_id[MAX_SEEN];
    int       score[MAX_SEEN];
    int       notification_code[MAX_SEEN];
    /* When set, the first match closes this handle. */
    audd_longpoll_t *close_on_match;
} rec_t;

static void copy_str(char *dst, const char *src)
{
    snprintf(dst, 128, "%s", src ? src : "(null)");
}

static void on_match(const audd_stream_callback_match_t *m, void *ud)
{
    rec_t *r = (rec_t *)ud;
    if (r->matches < MAX_SEEN) {
        const audd_stream_callback_song_t *s = audd_stream_callback_match_get_song(m);
        copy_str(r->artist[r->matches], audd_stream_callback_song_get_artist(s));
        copy_str(r->title[r->matches], audd_stream_callback_song_get_title(s));
        r->radio_id[r->matches] = (long long)audd_stream_callback_match_get_radio_id(m);
        r->score[r->matches] = audd_stream_callback_song_get_score(s);
    }
    r->matches++;
    if (r->close_on_match) audd_longpoll_close(r->close_on_match);
}

static void on_notification(const audd_stream_callback_notification_t *n, void *ud)
{
    rec_t *r = (rec_t *)ud;
    if (r->notifications < MAX_SEEN) {
        r->notification_code[r->notifications] = audd_stream_callback_notification_get_code(n);
    }
    r->notifications++;
}

static void on_error(audd_error_t err, const char *msg, void *ud)
{
    (void)err; (void)msg;
    ((rec_t *)ud)->errors++;
}

static rec_t g_rec;
static audd_longpoll_callbacks_t g_cb;

void setUp(void)
{
    memset(&g_rec, 0, sizeof(g_rec));
    g_cb.on_match = on_match;
    g_cb.on_notification = on_notification;
    g_cb.on_error = on_error;
    g_cb.user_data = &g_rec;
}

void tearDown(void)
{
    audd_set_allocator(NULL);
}

/* Process `text` as a longpoll body with since_time `since`. */
static audd_longpoll_page_t process_with(const char *text, int64_t since, audd_longpoll_t *h)
{
    audd_longpoll_page_t page;
    memset(&page, 0, sizeof(page));
    page.since = since;
    size_t len = text ? strlen(text) : 0;
    cJSON *json = text ? cJSON_ParseWithLength(text, len) : NULL;
    audd_longpoll_process_body(text, len, json, &g_cb, h, NULL, &page);
    cJSON_Delete(json);
    return page;
}

static audd_longpoll_page_t process(const char *text, int64_t since)
{
    return process_with(text, since, NULL);
}

static audd_longpoll_page_t process_fixture(const char *name, int64_t since)
{
    char *text = read_fixture(name, NULL);
    TEST_ASSERT_NOT_NULL_MESSAGE(text, name);
    audd_longpoll_page_t page = process(text, since);
    free(text);
    return page;
}

/* ------------------------------------------------------------------ *
 * The three fixtures                                                  *
 * ------------------------------------------------------------------ */

void test_events_fixture_delivers_both_songs_in_order(void)
{
    audd_longpoll_page_t page = process_fixture("longpoll_events.json", 0);
    TEST_ASSERT_EQUAL_INT(AUDD_OK, page.error);
    TEST_ASSERT_EQUAL_INT(2, g_rec.matches);
    TEST_ASSERT_EQUAL_STRING("Alan Walker, A$AP Rocky", g_rec.artist[0]);
    TEST_ASSERT_EQUAL_STRING("Live Fast (PUBGM)", g_rec.title[0]);
    TEST_ASSERT_EQUAL_INT64(7, g_rec.radio_id[0]);
    TEST_ASSERT_EQUAL_STRING("Tears For Fears", g_rec.artist[1]);
    TEST_ASSERT_EQUAL_STRING("Everybody Wants To Rule The World", g_rec.title[1]);
    TEST_ASSERT_EQUAL_INT64(7, g_rec.radio_id[1]);
    TEST_ASSERT_EQUAL_INT(100, g_rec.score[1]);
    TEST_ASSERT_EQUAL_INT(0, g_rec.notifications);
    TEST_ASSERT_EQUAL_INT(1, page.progressed);
    TEST_ASSERT_TRUE(page.since == INT64_C(1795547548112));
}

void test_timeout_fixture_delivers_nothing_and_moves_since(void)
{
    audd_longpoll_page_t page = process_fixture("longpoll_no_events.json", 0);
    TEST_ASSERT_EQUAL_INT(AUDD_OK, page.error);
    TEST_ASSERT_EQUAL_INT(0, g_rec.matches + g_rec.notifications);
    TEST_ASSERT_EQUAL_INT(1, page.progressed);
    TEST_ASSERT_TRUE(page.since == INT64_C(1777901270049));
}

void test_error_fixture_is_an_invalid_request(void)
{
    audd_longpoll_page_t page = process_fixture("longpoll_error.json", 0);
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_INVALID_REQUEST, page.error);
    TEST_ASSERT_EQUAL_INT(0, page.error_code);
    TEST_ASSERT_EQUAL_STRING("Invalid timeout arg.  Must be 1-120.", page.error_message);
    TEST_ASSERT_EQUAL_INT(0, g_rec.matches + g_rec.notifications + g_rec.errors);
}

/* ------------------------------------------------------------------ *
 * Events: shapes and edge cases                                       *
 * ------------------------------------------------------------------ */

#define MATCH_DATA(artist) \
    "{\"status\":\"success\",\"result\":{\"radio_id\":3,\"timestamp\":\"t\"," \
    "\"play_length\":10,\"results\":[{\"artist\":\"" artist "\",\"title\":\"T\",\"score\":90}]}}"

void test_data_sent_as_json_string_is_decoded(void)
{
    const char *body =
        "{\"events\":[{\"timestamp\":5,\"data\":"
        "\"{\\\"status\\\":\\\"success\\\",\\\"result\\\":{\\\"radio_id\\\":9,"
        "\\\"results\\\":[{\\\"artist\\\":\\\"S\\\",\\\"title\\\":\\\"Q\\\"}]}}\"}]}";
    audd_longpoll_page_t page = process(body, 0);
    TEST_ASSERT_EQUAL_INT(1, g_rec.matches);
    TEST_ASSERT_EQUAL_STRING("S", g_rec.artist[0]);
    TEST_ASSERT_EQUAL_INT64(9, g_rec.radio_id[0]);
    TEST_ASSERT_TRUE(page.since == 5);
}

void test_notification_event_goes_to_on_notification(void)
{
    const char *body =
        "{\"events\":[{\"timestamp\":11,\"data\":{\"status\":\"-\",\"notification\":"
        "{\"radio_id\":3,\"stream_running\":false,\"notification_code\":650,"
        "\"notification_message\":\"x\"},\"time\":1587939136}}]}";
    audd_longpoll_page_t page = process(body, 0);
    TEST_ASSERT_EQUAL_INT(0, g_rec.matches);
    TEST_ASSERT_EQUAL_INT(1, g_rec.notifications);
    TEST_ASSERT_EQUAL_INT(650, g_rec.notification_code[0]);
    TEST_ASSERT_TRUE(page.since == 11);
}

void test_unparseable_and_empty_events_are_skipped(void)
{
    const char *body =
        "{\"events\":["
        "{\"timestamp\":1,\"data\":{\"status\":\"success\",\"result\":{\"results\":[]}}},"
        "{\"timestamp\":2,\"data\":{\"status\":\"success\",\"result\":{\"radio_id\":1}}},"
        "{\"timestamp\":3,\"data\":\"not json\"},"
        "{\"timestamp\":4,\"data\":\"\"},"
        "{\"timestamp\":5,\"data\":null},"
        "{\"timestamp\":6,\"data\":42},"
        "{\"timestamp\":7,\"data\":[1,2]},"
        "{\"timestamp\":8},"
        "{\"timestamp\":9,\"data\":{\"unrelated\":true}},"
        "\"a string, not an event\","
        "17,"
        "null,"
        "{\"timestamp\":10,\"data\":" MATCH_DATA("Kept") "}"
        "]}";
    audd_longpoll_page_t page = process(body, 0);
    TEST_ASSERT_EQUAL_INT(AUDD_OK, page.error);
    TEST_ASSERT_EQUAL_INT(1, g_rec.matches);
    TEST_ASSERT_EQUAL_STRING("Kept", g_rec.artist[0]);
    TEST_ASSERT_TRUE(page.since == 10);
    TEST_ASSERT_EQUAL_INT(1, page.progressed);
}

void test_largest_timestamp_wins_and_odd_timestamps_are_ignored(void)
{
    const char *body =
        "{\"events\":["
        "{\"timestamp\":\"300\",\"data\":" MATCH_DATA("A") "},"
        "{\"timestamp\":200,\"data\":" MATCH_DATA("B") "},"
        "{\"timestamp\":-5,\"data\":" MATCH_DATA("C") "},"
        "{\"timestamp\":1e300,\"data\":" MATCH_DATA("D") "},"
        "{\"timestamp\":true,\"data\":" MATCH_DATA("E") "},"
        "{\"timestamp\":\"soon\",\"data\":" MATCH_DATA("F") "},"
        "{\"data\":" MATCH_DATA("G") "}"
        "]}";
    audd_longpoll_page_t page = process(body, 0);
    TEST_ASSERT_EQUAL_INT(7, g_rec.matches);
    TEST_ASSERT_EQUAL_STRING("A", g_rec.artist[0]);
    TEST_ASSERT_EQUAL_STRING("G", g_rec.artist[6]);
    TEST_ASSERT_TRUE(page.since == 300);
}

void test_events_without_a_new_timestamp_are_not_progress(void)
{
    /* Same timestamp as the request's since_time: delivered, but the loop
     * must not ask again at once. */
    audd_longpoll_page_t page = process("{\"events\":[{\"timestamp\":50,\"data\":" MATCH_DATA("A") "}]}", 50);
    TEST_ASSERT_EQUAL_INT(1, g_rec.matches);
    TEST_ASSERT_EQUAL_INT(0, page.progressed);
    TEST_ASSERT_TRUE(page.since == 50);

    page = process("{\"events\":[{\"data\":" MATCH_DATA("B") "}]}", 50);
    TEST_ASSERT_EQUAL_INT(2, g_rec.matches);
    TEST_ASSERT_EQUAL_INT(0, page.progressed);
}

void test_empty_or_wrong_typed_events_list_is_nothing_new(void)
{
    const char *bodies[] = {
        "{\"events\":[]}", "{\"events\":null}", "{\"events\":{}}",
        "{\"events\":\"x\"}", "{\"events\":[], \"timestamp\":99}", NULL
    };
    for (int i = 0; bodies[i] != NULL; ++i) {
        audd_longpoll_page_t page = process(bodies[i], 7);
        TEST_ASSERT_EQUAL_INT_MESSAGE(AUDD_OK, page.error, bodies[i]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, page.progressed, bodies[i]);
        TEST_ASSERT_TRUE_MESSAGE(page.since == 7, bodies[i]);
    }
    TEST_ASSERT_EQUAL_INT(0, g_rec.matches + g_rec.notifications);
}

void test_wrong_typed_fields_inside_data_degrade(void)
{
    const char *body =
        "{\"events\":[{\"timestamp\":1,\"data\":{\"status\":\"success\",\"result\":"
        "{\"radio_id\":\"12\",\"play_length\":\"x\",\"timestamp\":5,"
        "\"results\":[{\"artist\":77,\"title\":null,\"score\":1e300}]}}}]}";
    process(body, 0);
    TEST_ASSERT_EQUAL_INT(1, g_rec.matches);
    TEST_ASSERT_EQUAL_STRING("77", g_rec.artist[0]);
    TEST_ASSERT_EQUAL_STRING("(null)", g_rec.title[0]);
    TEST_ASSERT_EQUAL_INT64(12, g_rec.radio_id[0]);
    TEST_ASSERT_EQUAL_INT(0, g_rec.score[0]);
}

void test_close_during_delivery_stops_the_page(void)
{
    audd_longpoll_t *h = audd_longpoll_new();
    TEST_ASSERT_NOT_NULL(h);
    g_rec.close_on_match = h;
    char *text = read_fixture("longpoll_events.json", NULL);
    TEST_ASSERT_NOT_NULL(text);
    process_with(text, 0, h);
    free(text);
    TEST_ASSERT_EQUAL_INT(1, g_rec.matches);
    audd_longpoll_free(h);
}

/* ------------------------------------------------------------------ *
 * Top-level (callback-shaped) bodies, timeout and junk                *
 * ------------------------------------------------------------------ */

void test_top_level_result_still_delivers(void)
{
    audd_longpoll_page_t page = process(
        "{\"status\":\"success\",\"result\":{\"radio_id\":4,\"results\":"
        "[{\"artist\":\"Top\",\"title\":\"Level\"}]},\"timestamp\":1234}", 0);
    TEST_ASSERT_EQUAL_INT(1, g_rec.matches);
    TEST_ASSERT_EQUAL_STRING("Top", g_rec.artist[0]);
    TEST_ASSERT_TRUE(page.since == 1234);
    TEST_ASSERT_EQUAL_INT(1, page.progressed);

    /* Without a timestamp: delivered, since unchanged, still progress. */
    page = process("{\"status\":\"success\",\"result\":{\"results\":[{\"artist\":\"B\"}]}}", 77);
    TEST_ASSERT_EQUAL_INT(2, g_rec.matches);
    TEST_ASSERT_TRUE(page.since == 77);
    TEST_ASSERT_EQUAL_INT(1, page.progressed);

    /* Top-level notification. */
    page = process("{\"status\":\"-\",\"notification\":{\"notification_code\":651}}", 77);
    TEST_ASSERT_EQUAL_INT(1, g_rec.notifications);
    TEST_ASSERT_EQUAL_INT(651, g_rec.notification_code[0]);
}

void test_top_level_result_with_no_songs_is_skipped(void)
{
    audd_longpoll_page_t page = process(
        "{\"status\":\"success\",\"result\":{\"radio_id\":4,\"results\":[]}}", 5);
    TEST_ASSERT_EQUAL_INT(0, g_rec.matches);
    TEST_ASSERT_EQUAL_INT(0, page.progressed);
}

void test_timeout_without_timestamp_is_nothing_new(void)
{
    audd_longpoll_page_t page = process("{\"timeout\":\"no events before timeout\"}", 3);
    TEST_ASSERT_EQUAL_INT(0, page.progressed);
    TEST_ASSERT_TRUE(page.since == 3);
    page = process("{\"timeout\":\"no events before timeout\",\"timestamp\":\"88\"}", 3);
    TEST_ASSERT_EQUAL_INT(1, page.progressed);
    TEST_ASSERT_TRUE(page.since == 88);
}

void test_junk_bodies_are_nothing_new(void)
{
    const char *bodies[] = {
        "", "<html>502</html>", "[]", "[{\"events\":[]}]", "42", "\"str\"", "null",
        "{}", "{\"timestamp\":123}", "{\"status\":\"success\"}", "{\"events\"", NULL
    };
    for (int i = 0; bodies[i] != NULL; ++i) {
        audd_longpoll_page_t page = process(bodies[i], 9);
        TEST_ASSERT_EQUAL_INT_MESSAGE(AUDD_OK, page.error, bodies[i]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, page.progressed, bodies[i]);
        TEST_ASSERT_TRUE_MESSAGE(page.since == 9, bodies[i]);
    }
    audd_longpoll_page_t page = process(NULL, 9);
    TEST_ASSERT_EQUAL_INT(0, page.progressed);
    TEST_ASSERT_EQUAL_INT(0, g_rec.matches + g_rec.notifications + g_rec.errors);
}

/* ------------------------------------------------------------------ *
 * Error bodies                                                        *
 * ------------------------------------------------------------------ */

void test_error_block_maps_by_code(void)
{
    audd_longpoll_page_t page = process(
        "{\"status\":\"error\",\"error\":{\"error_code\":900,\"error_message\":\"bad token\"}}", 0);
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_AUTHENTICATION, page.error);
    TEST_ASSERT_EQUAL_INT(900, page.error_code);
    TEST_ASSERT_EQUAL_STRING("[#900] bad token", page.error_message);

    page = process("{\"error\":{\"error_code\":\"906\"}}", 0);
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_INVALID_REQUEST, page.error);
    TEST_ASSERT_EQUAL_INT(906, page.error_code);

    page = process("{\"error\":{\"error_message\":\"no code\"}}", 0);
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_INVALID_REQUEST, page.error);
    TEST_ASSERT_EQUAL_INT(0, page.error_code);
    TEST_ASSERT_EQUAL_STRING("no code", page.error_message);
}

void test_odd_error_values(void)
{
    audd_longpoll_page_t page = process("{\"error\":\"\"}", 0);
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_INVALID_REQUEST, page.error);
    TEST_ASSERT_EQUAL_STRING("Longpoll request rejected", page.error_message);

    page = process("{\"error\":\"  \"}", 0);
    TEST_ASSERT_EQUAL_STRING("Longpoll request rejected", page.error_message);

    page = process("{\"error\":{}}", 0);
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_INVALID_REQUEST, page.error);
    TEST_ASSERT_EQUAL_STRING("Longpoll request rejected", page.error_message);

    page = process("{\"error\":42}", 0);
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_INVALID_REQUEST, page.error);
    TEST_ASSERT_EQUAL_STRING("42", page.error_message);

    /* An error wins over events in the same body. */
    page = process("{\"error\":\"x\",\"events\":[{\"timestamp\":1,\"data\":" MATCH_DATA("A") "}]}", 0);
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_INVALID_REQUEST, page.error);
    TEST_ASSERT_EQUAL_INT(0, g_rec.matches);

    /* A null error is no error. */
    page = process("{\"error\":null,\"timeout\":\"no events before timeout\",\"timestamp\":5}", 0);
    TEST_ASSERT_EQUAL_INT(AUDD_OK, page.error);
    TEST_ASSERT_TRUE(page.since == 5);
}

void test_long_error_text_is_truncated_safely(void)
{
    size_t n = 5000;
    char *body = (char *)malloc(n + 32);
    TEST_ASSERT_NOT_NULL(body);
    strcpy(body, "{\"error\":\"");
    size_t off = strlen(body);
    memset(body + off, 'e', n);
    strcpy(body + off + n, "\"}");
    audd_longpoll_page_t page = process(body, 0);
    free(body);
    TEST_ASSERT_EQUAL_INT(AUDD_ERR_INVALID_REQUEST, page.error);
    TEST_ASSERT_EQUAL_INT(sizeof(page.error_message) - 1, strlen(page.error_message));
}

/* ------------------------------------------------------------------ *
 * Backoff                                                             *
 * ------------------------------------------------------------------ */

void test_backoff_doubles_from_one_second_to_thirty(void)
{
    long d = AUDD_LONGPOLL_BACKOFF_START_MS;
    TEST_ASSERT_EQUAL_INT(1000, (int)d);
    const long want[] = { 2000, 4000, 8000, 16000, 30000, 30000, 30000 };
    for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); ++i) {
        d = audd_longpoll_next_backoff(d, AUDD_LONGPOLL_BACKOFF_MAX_MS);
        TEST_ASSERT_EQUAL_INT((int)want[i], (int)d);
    }
}

/* ------------------------------------------------------------------ *
 * Allocation failure                                                  *
 * ------------------------------------------------------------------ */

static long g_alloc_calls;
static long g_fail_at;      /* 1-based allocation number to fail; 0 = never */
static int  g_fail_rest;    /* also fail every allocation after it */

static void *failing_malloc(size_t n)
{
    ++g_alloc_calls;
    if (g_fail_at != 0 &&
        (g_alloc_calls == g_fail_at || (g_fail_rest && g_alloc_calls > g_fail_at))) {
        return NULL;
    }
    return malloc(n);
}
static void failing_free(void *p) { free(p); }

/* Process `text` once for every allocation it makes, failing that
 * allocation (and, with fail_rest, every later one). Nothing may crash or
 * leak; with no failure, `want_matches` songs are delivered. `min_allocs`
 * guards against a sweep that never failed anything. */
static void sweep(const char *text, int want_matches, int fail_rest, long min_allocs)
{
    audd_allocator_t a = { failing_malloc, failing_free, NULL };
    for (long fail_at = 1; fail_at < 10000; ++fail_at) {
        cJSON *json = cJSON_Parse(text); /* default allocator */
        TEST_ASSERT_NOT_NULL(json);
        memset(&g_rec, 0, sizeof(g_rec));
        g_alloc_calls = 0;
        g_fail_at = fail_at;
        g_fail_rest = fail_rest;
        audd_set_allocator(&a);
        audd_longpoll_page_t page;
        memset(&page, 0, sizeof(page));
        audd_longpoll_process_body(text, strlen(text), json, &g_cb, NULL, NULL, &page);
        audd_set_allocator(NULL);
        cJSON_Delete(json);
        TEST_ASSERT_TRUE(g_rec.matches <= want_matches);
        if (g_alloc_calls < fail_at) {
            /* No allocation failed this time: the full result. */
            TEST_ASSERT_EQUAL_INT(want_matches, g_rec.matches);
            TEST_ASSERT_TRUE_MESSAGE(fail_at > min_allocs, "too few allocations were failed");
            return;
        }
    }
    TEST_FAIL_MESSAGE("allocation sweep did not finish");
}

void test_allocation_failure_events_fixture(void)
{
    char *text = read_fixture("longpoll_events.json", NULL);
    TEST_ASSERT_NOT_NULL(text);
    sweep(text, 2, 0, 20);
    sweep(text, 2, 1, 20);
    free(text);
}

void test_allocation_failure_string_data_and_top_level(void)
{
    const char *string_data =
        "{\"events\":[{\"timestamp\":5,\"data\":"
        "\"{\\\"status\\\":\\\"success\\\",\\\"result\\\":{\\\"radio_id\\\":9,"
        "\\\"results\\\":[{\\\"artist\\\":\\\"S\\\",\\\"title\\\":\\\"Q\\\"},"
        "{\\\"artist\\\":\\\"S2\\\",\\\"title\\\":\\\"Q2\\\"}]}}\"},"
        "{\"timestamp\":6,\"data\":{\"notification\":{\"notification_code\":650,"
        "\"notification_message\":\"m\"}}}]}";
    sweep(string_data, 1, 0, 10);
    sweep(string_data, 1, 1, 10);
    const char *top = "{\"status\":\"success\",\"result\":{\"radio_id\":4,\"results\":"
                      "[{\"artist\":\"Top\",\"title\":\"Level\",\"score\":\"85\"}]},\"timestamp\":1234}";
    sweep(top, 1, 0, 5);
    sweep(top, 1, 1, 5);
    sweep("{\"error\":{\"error_code\":900,\"error_message\":77}}", 0, 0, 1);
    sweep("{\"error\":[1,2,3]}", 0, 1, 1);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_events_fixture_delivers_both_songs_in_order);
    RUN_TEST(test_timeout_fixture_delivers_nothing_and_moves_since);
    RUN_TEST(test_error_fixture_is_an_invalid_request);
    RUN_TEST(test_data_sent_as_json_string_is_decoded);
    RUN_TEST(test_notification_event_goes_to_on_notification);
    RUN_TEST(test_unparseable_and_empty_events_are_skipped);
    RUN_TEST(test_largest_timestamp_wins_and_odd_timestamps_are_ignored);
    RUN_TEST(test_events_without_a_new_timestamp_are_not_progress);
    RUN_TEST(test_empty_or_wrong_typed_events_list_is_nothing_new);
    RUN_TEST(test_wrong_typed_fields_inside_data_degrade);
    RUN_TEST(test_close_during_delivery_stops_the_page);
    RUN_TEST(test_top_level_result_still_delivers);
    RUN_TEST(test_top_level_result_with_no_songs_is_skipped);
    RUN_TEST(test_timeout_without_timestamp_is_nothing_new);
    RUN_TEST(test_junk_bodies_are_nothing_new);
    RUN_TEST(test_error_block_maps_by_code);
    RUN_TEST(test_odd_error_values);
    RUN_TEST(test_long_error_text_is_truncated_safely);
    RUN_TEST(test_backoff_doubles_from_one_second_to_thirty);
    RUN_TEST(test_allocation_failure_events_fixture);
    RUN_TEST(test_allocation_failure_string_data_and_top_level);
    return UNITY_END();
}
