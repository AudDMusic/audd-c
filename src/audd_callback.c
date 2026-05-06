/* audd_callback.c — parse stream callback bodies into typed structs. */
#include "audd_internal.h"
#include "audd.h"

#include <stdlib.h>
#include <string.h>

#include "../vendor/cJSON/cJSON.h"

struct audd_stream_callback_song {
    int   score;
    char *artist;
    char *title;
    char *album;
    char *release_date;
    char *label;
    char *song_link;
    char *isrc;
    char *upc;
};

struct audd_stream_callback_match {
    int64_t radio_id;
    char   *timestamp;
    int     play_length;
    audd_stream_callback_song_t  song;
    audd_stream_callback_song_t *alternatives;
    size_t  alt_count;
    char   *raw_response; /* heap copy of body */
};

struct audd_stream_callback_notification {
    int   radio_id;
    int   stream_running; /* -1 absent, 0 false, 1 true */
    int   notification_code;
    char *notification_message;
    int   time;
    char *raw_response;
};

static char *sdup(const char *s) { return s ? audd_strdup(s) : NULL; }

static void parse_song(audd_stream_callback_song_t *out, const cJSON *o)
{
    memset(out, 0, sizeof(*out));
    if (!cJSON_IsObject(o)) return;
    out->score = audd_json_get_int(o, "score", 0);
    out->artist = sdup(audd_json_get_string(o, "artist"));
    out->title = sdup(audd_json_get_string(o, "title"));
    out->album = sdup(audd_json_get_string(o, "album"));
    out->release_date = sdup(audd_json_get_string(o, "release_date"));
    out->label = sdup(audd_json_get_string(o, "label"));
    out->song_link = sdup(audd_json_get_string(o, "song_link"));
    out->isrc = sdup(audd_json_get_string(o, "isrc"));
    out->upc = sdup(audd_json_get_string(o, "upc"));
}

static void free_song_inner(audd_stream_callback_song_t *s)
{
    audd_free(s->artist);
    audd_free(s->title);
    audd_free(s->album);
    audd_free(s->release_date);
    audd_free(s->label);
    audd_free(s->song_link);
    audd_free(s->isrc);
    audd_free(s->upc);
    memset(s, 0, sizeof(*s));
}

void audd_stream_callback_match_free(audd_stream_callback_match_t *m)
{
    if (m == NULL) return;
    free_song_inner(&m->song);
    if (m->alternatives) {
        for (size_t i = 0; i < m->alt_count; ++i) free_song_inner(&m->alternatives[i]);
        audd_free(m->alternatives);
    }
    audd_free(m->timestamp);
    audd_free(m->raw_response);
    audd_free(m);
}

void audd_stream_callback_notification_free(audd_stream_callback_notification_t *n)
{
    if (n == NULL) return;
    audd_free(n->notification_message);
    audd_free(n->raw_response);
    audd_free(n);
}

audd_error_t audd_parse_callback(const void *body, size_t size,
                                 audd_stream_callback_match_t **out_match,
                                 audd_stream_callback_notification_t **out_notification,
                                 char **out_error_message)
{
    if (out_match == NULL || out_notification == NULL) return AUDD_ERR_INVALID_ARGUMENT;
    *out_match = NULL;
    *out_notification = NULL;
    if (out_error_message) *out_error_message = NULL;
    if (body == NULL || size == 0) {
        if (out_error_message) *out_error_message = audd_strdup("audd: empty callback body");
        return AUDD_ERR_SERIALIZATION;
    }
    cJSON *root = cJSON_ParseWithLength((const char *)body, size);
    if (root == NULL) {
        if (out_error_message) *out_error_message = audd_strdup("audd: callback body is not valid JSON");
        return AUDD_ERR_SERIALIZATION;
    }

    cJSON *notif = cJSON_GetObjectItemCaseSensitive(root, "notification");
    if (cJSON_IsObject(notif)) {
        audd_stream_callback_notification_t *n =
            (audd_stream_callback_notification_t *)audd_malloc(sizeof(*n));
        if (n == NULL) {
            cJSON_Delete(root);
            return AUDD_ERR_OUT_OF_MEMORY;
        }
        memset(n, 0, sizeof(*n));
        n->radio_id = audd_json_get_int(notif, "radio_id", 0);
        cJSON *sr = cJSON_GetObjectItemCaseSensitive(notif, "stream_running");
        if (sr == NULL || cJSON_IsNull(sr)) n->stream_running = -1;
        else n->stream_running = audd_json_get_bool(notif, "stream_running", 0);
        n->notification_code = audd_json_get_int(notif, "notification_code", 0);
        n->notification_message = sdup(audd_json_get_string(notif, "notification_message"));
        n->time = audd_json_get_int(root, "time", 0);
        n->raw_response = audd_strndup((const char *)body, size);
        *out_notification = n;
        cJSON_Delete(root);
        return AUDD_OK;
    }

    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (cJSON_IsObject(result)) {
        cJSON *results = cJSON_GetObjectItemCaseSensitive(result, "results");
        if (!cJSON_IsArray(results) || cJSON_GetArraySize(results) == 0) {
            if (out_error_message) *out_error_message = audd_strdup("audd: callback result.results is empty");
            cJSON_Delete(root);
            return AUDD_ERR_SERIALIZATION;
        }
        audd_stream_callback_match_t *m =
            (audd_stream_callback_match_t *)audd_malloc(sizeof(*m));
        if (m == NULL) { cJSON_Delete(root); return AUDD_ERR_OUT_OF_MEMORY; }
        memset(m, 0, sizeof(*m));
        m->radio_id = audd_json_get_int64(result, "radio_id", 0);
        m->timestamp = sdup(audd_json_get_string(result, "timestamp"));
        m->play_length = audd_json_get_int(result, "play_length", 0);

        int n = cJSON_GetArraySize(results);
        parse_song(&m->song, cJSON_GetArrayItem(results, 0));
        if (n > 1) {
            m->alternatives = (audd_stream_callback_song_t *)audd_malloc(
                (size_t)(n - 1) * sizeof(*m->alternatives));
            if (m->alternatives == NULL) {
                audd_stream_callback_match_free(m);
                cJSON_Delete(root);
                return AUDD_ERR_OUT_OF_MEMORY;
            }
            memset(m->alternatives, 0, (size_t)(n - 1) * sizeof(*m->alternatives));
            m->alt_count = (size_t)(n - 1);
            for (int i = 1; i < n; ++i) {
                parse_song(&m->alternatives[i-1], cJSON_GetArrayItem(results, i));
            }
        }
        m->raw_response = audd_strndup((const char *)body, size);
        *out_match = m;
        cJSON_Delete(root);
        return AUDD_OK;
    }

    if (out_error_message) *out_error_message = audd_strdup("audd: callback body has neither result nor notification");
    cJSON_Delete(root);
    return AUDD_ERR_SERIALIZATION;
}

/* ---------------- match getters ---------------- */

int64_t audd_stream_callback_match_get_radio_id(const audd_stream_callback_match_t *m)
{ return m ? m->radio_id : 0; }
const char *audd_stream_callback_match_get_timestamp(const audd_stream_callback_match_t *m)
{ return m ? m->timestamp : NULL; }
int audd_stream_callback_match_get_play_length(const audd_stream_callback_match_t *m)
{ return m ? m->play_length : 0; }
const audd_stream_callback_song_t *audd_stream_callback_match_get_song(const audd_stream_callback_match_t *m)
{ return m ? &m->song : NULL; }
size_t audd_stream_callback_match_alternatives_count(const audd_stream_callback_match_t *m)
{ return m ? m->alt_count : 0; }
const audd_stream_callback_song_t *audd_stream_callback_match_get_alternative(const audd_stream_callback_match_t *m, size_t i)
{
    if (m == NULL || i >= m->alt_count) return NULL;
    return &m->alternatives[i];
}
const char *audd_stream_callback_match_raw_response(const audd_stream_callback_match_t *m)
{ return m ? m->raw_response : NULL; }

#define SONG_GETTER_STR(field) \
    const char *audd_stream_callback_song_get_##field(const audd_stream_callback_song_t *s) \
    { return s ? s->field : NULL; }

int audd_stream_callback_song_get_score(const audd_stream_callback_song_t *s)
{ return s ? s->score : 0; }
SONG_GETTER_STR(artist)
SONG_GETTER_STR(title)
SONG_GETTER_STR(album)
SONG_GETTER_STR(release_date)
SONG_GETTER_STR(label)
SONG_GETTER_STR(song_link)
SONG_GETTER_STR(isrc)
SONG_GETTER_STR(upc)

/* ---------------- notification getters ---------------- */

int audd_stream_callback_notification_get_radio_id(const audd_stream_callback_notification_t *n)
{ return n ? n->radio_id : 0; }
int audd_stream_callback_notification_get_stream_running(const audd_stream_callback_notification_t *n)
{ return n ? n->stream_running : -1; }
int audd_stream_callback_notification_get_code(const audd_stream_callback_notification_t *n)
{ return n ? n->notification_code : 0; }
const char *audd_stream_callback_notification_get_message(const audd_stream_callback_notification_t *n)
{ return n ? n->notification_message : NULL; }
int audd_stream_callback_notification_get_time(const audd_stream_callback_notification_t *n)
{ return n ? n->time : 0; }
const char *audd_stream_callback_notification_raw_response(const audd_stream_callback_notification_t *n)
{ return n ? n->raw_response : NULL; }
