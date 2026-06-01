/* audd_enterprise.c — EnterpriseMatch + EnterpriseResult containers. */
#include "audd_internal.h"
#include "audd.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "../vendor/cJSON/cJSON.h"

struct audd_enterprise_match {
    int   score;
    char *timecode;
    char *artist;
    char *title;
    char *album;
    char *release_date;
    char *label;
    char *isrc;
    char *upc;
    char *song_link;
    int   start_offset;
    int   end_offset;
    /* Position of the match in the user's file, in seconds. -1.0 = unknown
     * (the chunk carried no usable offset). Real file positions are >= 0. */
    double start_seconds;
    double end_seconds;
    char *thumbnail_url;          /* lazy */
    char *streaming_urls[5];      /* lazy */
    int   thumb_lazy;
    int   stream_lazy[5];
};

struct audd_enterprise_result {
    audd_enterprise_match_t *items;
    size_t count;
};

static char *strdup_or_null(const char *s)
{
    return (s == NULL) ? NULL : audd_strdup(s);
}

/* Parse a chunk offset string into seconds. Accepts "SS", "MM:SS",
 * "HH:MM:SS", or a bare number. Returns -1.0 on NULL/empty/unparseable.
 * Never crashes. */
static double audd_offset_to_seconds(const char *offset)
{
    if (offset == NULL) return -1.0;
    while (*offset == ' ' || *offset == '\t') offset++;
    if (*offset == '\0') return -1.0;

    /* Bare number (no colon): seconds, possibly fractional. */
    if (strchr(offset, ':') == NULL) {
        char *end = NULL;
        double v = strtod(offset, &end);
        if (end == offset) return -1.0;
        while (*end == ' ' || *end == '\t') end++;
        if (*end != '\0') return -1.0;
        return (v < 0.0) ? -1.0 : v;
    }

    /* Colon-separated: up to three components, last one may be fractional. */
    double parts[3];
    int n = 0;
    const char *p = offset;
    while (n < 3) {
        char *end = NULL;
        double v = strtod(p, &end);
        if (end == p) return -1.0;        /* empty component */
        if (v < 0.0) return -1.0;
        parts[n++] = v;
        p = end;
        if (*p == ':') { p++; continue; }
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;
        return -1.0;                       /* trailing garbage */
    }
    if (*p == ':') return -1.0;            /* more than three components */

    double seconds = 0.0;
    for (int i = 0; i < n; ++i) {
        seconds = seconds * 60.0 + parts[i];
    }
    return seconds;
}

static void match_free_inner(audd_enterprise_match_t *m)
{
    if (m == NULL) return;
    audd_free(m->timecode);
    audd_free(m->artist);
    audd_free(m->title);
    audd_free(m->album);
    audd_free(m->release_date);
    audd_free(m->label);
    audd_free(m->isrc);
    audd_free(m->upc);
    audd_free(m->song_link);
    audd_free(m->thumbnail_url);
    for (int i = 0; i < 5; ++i) audd_free(m->streaming_urls[i]);
}

static void match_from_json(audd_enterprise_match_t *m, const cJSON *o, double base)
{
    memset(m, 0, sizeof(*m));
    m->start_seconds = -1.0;
    m->end_seconds = -1.0;
    m->score = audd_json_get_int(o, "score", 0);
    m->timecode = strdup_or_null(audd_json_get_string(o, "timecode"));
    m->artist = strdup_or_null(audd_json_get_string(o, "artist"));
    m->title = strdup_or_null(audd_json_get_string(o, "title"));
    m->album = strdup_or_null(audd_json_get_string(o, "album"));
    m->release_date = strdup_or_null(audd_json_get_string(o, "release_date"));
    m->label = strdup_or_null(audd_json_get_string(o, "label"));
    m->isrc = strdup_or_null(audd_json_get_string(o, "isrc"));
    m->upc = strdup_or_null(audd_json_get_string(o, "upc"));
    m->song_link = strdup_or_null(audd_json_get_string(o, "song_link"));
    m->start_offset = audd_json_get_int(o, "start_offset", 0);
    m->end_offset = audd_json_get_int(o, "end_offset", 0);
    /* The chunk offset places the fragment in the user's file; start_offset/
     * end_offset are milliseconds within the fragment. Combine them when the
     * chunk offset is known; otherwise leave the seconds fields at -1.0. */
    if (base >= 0.0) {
        m->start_seconds = base + m->start_offset / 1000.0;
        m->end_seconds = base + m->end_offset / 1000.0;
    }
}

audd_enterprise_result_t *audd_enterprise_from_json(const cJSON *result_arr)
{
    audd_enterprise_result_t *r = (audd_enterprise_result_t *)audd_malloc(sizeof(*r));
    if (r == NULL) return NULL;
    memset(r, 0, sizeof(*r));
    if (!cJSON_IsArray(result_arr)) {
        return r; /* empty result */
    }
    /* Each element is an object with "songs" array. Flatten. */
    int chunks = cJSON_GetArraySize((cJSON *)result_arr);
    /* First: count total matches. */
    size_t total = 0;
    for (int i = 0; i < chunks; ++i) {
        cJSON *chunk = cJSON_GetArrayItem((cJSON *)result_arr, i);
        if (!cJSON_IsObject(chunk)) continue;
        cJSON *songs = cJSON_GetObjectItemCaseSensitive(chunk, "songs");
        if (cJSON_IsArray(songs)) {
            total += (size_t)cJSON_GetArraySize(songs);
        }
    }
    if (total == 0) return r;
    r->items = (audd_enterprise_match_t *)audd_malloc(total * sizeof(*r->items));
    if (r->items == NULL) {
        audd_free(r);
        return NULL;
    }
    memset(r->items, 0, total * sizeof(*r->items));
    /* Fill. */
    for (int i = 0; i < chunks; ++i) {
        cJSON *chunk = cJSON_GetArrayItem((cJSON *)result_arr, i);
        if (!cJSON_IsObject(chunk)) continue;
        cJSON *songs = cJSON_GetObjectItemCaseSensitive(chunk, "songs");
        if (!cJSON_IsArray(songs)) continue;
        double base = audd_offset_to_seconds(audd_json_get_string(chunk, "offset"));
        int n = cJSON_GetArraySize(songs);
        for (int j = 0; j < n; ++j) {
            cJSON *song = cJSON_GetArrayItem(songs, j);
            if (!cJSON_IsObject(song)) continue;
            match_from_json(&r->items[r->count++], song, base);
        }
    }
    return r;
}

void audd_enterprise_result_free(audd_enterprise_result_t *r)
{
    if (r == NULL) return;
    for (size_t i = 0; i < r->count; ++i) {
        match_free_inner(&r->items[i]);
    }
    audd_free(r->items);
    audd_free(r);
}

size_t audd_enterprise_result_count(const audd_enterprise_result_t *r)
{
    return r ? r->count : 0;
}

const audd_enterprise_match_t *audd_enterprise_result_at(const audd_enterprise_result_t *r, size_t i)
{
    if (r == NULL || i >= r->count) return NULL;
    return &r->items[i];
}

#define GETTER_INT(field) \
    int audd_enterprise_match_get_##field(const audd_enterprise_match_t *m) \
    { return m ? m->field : 0; }
#define GETTER_STR(field) \
    const char *audd_enterprise_match_get_##field(const audd_enterprise_match_t *m) \
    { return m ? m->field : NULL; }
#define GETTER_DBL(field) \
    double audd_enterprise_match_get_##field(const audd_enterprise_match_t *m) \
    { return m ? m->field : -1.0; }

GETTER_INT(score)
GETTER_STR(timecode)
GETTER_STR(artist)
GETTER_STR(title)
GETTER_STR(album)
GETTER_STR(release_date)
GETTER_STR(label)
GETTER_STR(isrc)
GETTER_STR(upc)
GETTER_STR(song_link)
GETTER_INT(start_offset)
GETTER_INT(end_offset)
GETTER_DBL(start_seconds)
GETTER_DBL(end_seconds)

static const char *provider_str(audd_provider_t p)
{
    switch (p) {
    case AUDD_PROVIDER_SPOTIFY: return "spotify";
    case AUDD_PROVIDER_APPLE_MUSIC: return "apple_music";
    case AUDD_PROVIDER_DEEZER: return "deezer";
    case AUDD_PROVIDER_NAPSTER: return "napster";
    case AUDD_PROVIDER_YOUTUBE: return "youtube";
    }
    return "";
}

const char *audd_enterprise_match_thumbnail_url(const audd_enterprise_match_t *m)
{
    if (m == NULL) return NULL;
    audd_enterprise_match_t *mm = (audd_enterprise_match_t *)m; /* lazy mut */
    if (mm->thumb_lazy) return mm->thumbnail_url;
    mm->thumb_lazy = 1;
    if (!audd_url_hostname_is(mm->song_link, "lis.tn")) return NULL;
    char sep = (strchr(mm->song_link, '?') == NULL) ? '?' : '&';
    mm->thumbnail_url = audd_aprintf("%s%cthumb", mm->song_link, sep);
    return mm->thumbnail_url;
}

const char *audd_enterprise_match_streaming_url(const audd_enterprise_match_t *m, audd_provider_t p)
{
    if (m == NULL) return NULL;
    if ((unsigned)p > AUDD_PROVIDER_YOUTUBE) return NULL;
    audd_enterprise_match_t *mm = (audd_enterprise_match_t *)m;
    if (mm->stream_lazy[p]) return mm->streaming_urls[p];
    mm->stream_lazy[p] = 1;
    if (!audd_url_hostname_is(mm->song_link, "lis.tn")) return NULL;
    char sep = (strchr(mm->song_link, '?') == NULL) ? '?' : '&';
    mm->streaming_urls[p] = audd_aprintf("%s%c%s", mm->song_link, sep, provider_str(p));
    return mm->streaming_urls[p];
}
