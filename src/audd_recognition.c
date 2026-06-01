/* audd_recognition.c — recognize() + recognize_enterprise() + result types. */
#include "audd_internal.h"
#include "audd.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../vendor/cJSON/cJSON.h"

static const char *kApiBase = "https://api.audd.io";
static const char *kEnterpriseBase = "https://enterprise.audd.io";

/* ---------------- metadata block structs ---------------- */

struct audd_apple_music {
    char *artist_name;
    char *url;
    int   duration_ms;
    char *name;
    char *isrc;
    char *album_name;
    int   track_number;
    char *composer_name;
    int   disc_number;
    char *release_date;
    cJSON *raw; /* owns; printed via cJSON_Print on extras lookup */
};

struct audd_spotify {
    char *id;
    char *name;
    int   duration_ms;
    int   explicit_;
    int   popularity;
    int   track_number;
    char *type;
    char *uri;
    cJSON *raw;
};

struct audd_deezer {
    int   id;
    char *title;
    int   duration;
    char *link;
    cJSON *raw;
};

struct audd_napster {
    char *id;
    char *name;
    char *isrc;
    char *artist_name;
    char *album_name;
    cJSON *raw;
};

struct audd_musicbrainz_entry {
    char *id;
    char *score;
    char *title;
    int   length;
};

struct audd_musicbrainz_list {
    audd_musicbrainz_entry_t *items;
    size_t count;
};

/* ---------------- recognition struct ---------------- */

struct audd_recognition {
    char *timecode;
    char *artist;
    char *title;
    char *album;
    char *release_date;
    char *label;
    char *song_link;
    char *isrc;
    char *upc;

    int  has_audio_id;
    int  audio_id;

    audd_apple_music_t *apple_music;
    audd_spotify_t     *spotify;
    audd_deezer_t      *deezer;
    audd_napster_t     *napster;
    struct audd_musicbrainz_list mb;

    cJSON *raw_obj;          /* full result block — for extras + raw access */
    char *raw_response_str;  /* cached printed form */

    /* lazy-built helpers */
    char *thumbnail_url;
    char *streaming_urls[5]; /* indexed by audd_provider_t */
    char *preview_url;
    int   streaming_lazy[5];
    int   thumbnail_lazy;
    int   preview_lazy;
};

static char *strdup_or_null(const char *s)
{
    if (s == NULL) return NULL;
    return audd_strdup(s);
}

/* ---------------- block parsers ---------------- */

static audd_apple_music_t *parse_apple_music(const cJSON *o)
{
    if (!cJSON_IsObject(o)) return NULL;
    audd_apple_music_t *m = (audd_apple_music_t *)audd_malloc(sizeof(*m));
    if (m == NULL) return NULL;
    memset(m, 0, sizeof(*m));
    m->artist_name = strdup_or_null(audd_json_get_string(o, "artistName"));
    m->url = strdup_or_null(audd_json_get_string(o, "url"));
    m->duration_ms = audd_json_get_int(o, "durationInMillis", 0);
    m->name = strdup_or_null(audd_json_get_string(o, "name"));
    m->isrc = strdup_or_null(audd_json_get_string(o, "isrc"));
    m->album_name = strdup_or_null(audd_json_get_string(o, "albumName"));
    m->track_number = audd_json_get_int(o, "trackNumber", 0);
    m->composer_name = strdup_or_null(audd_json_get_string(o, "composerName"));
    m->disc_number = audd_json_get_int(o, "discNumber", 0);
    m->release_date = strdup_or_null(audd_json_get_string(o, "releaseDate"));
    m->raw = cJSON_Duplicate(o, 1);
    return m;
}

static void free_apple_music(audd_apple_music_t *m)
{
    if (m == NULL) return;
    audd_free(m->artist_name);
    audd_free(m->url);
    audd_free(m->name);
    audd_free(m->isrc);
    audd_free(m->album_name);
    audd_free(m->composer_name);
    audd_free(m->release_date);
    if (m->raw) cJSON_Delete(m->raw);
    audd_free(m);
}

static audd_spotify_t *parse_spotify(const cJSON *o)
{
    if (!cJSON_IsObject(o)) return NULL;
    audd_spotify_t *m = (audd_spotify_t *)audd_malloc(sizeof(*m));
    if (m == NULL) return NULL;
    memset(m, 0, sizeof(*m));
    m->id = strdup_or_null(audd_json_get_string(o, "id"));
    m->name = strdup_or_null(audd_json_get_string(o, "name"));
    m->duration_ms = audd_json_get_int(o, "duration_ms", 0);
    m->explicit_ = audd_json_get_bool(o, "explicit", 0);
    m->popularity = audd_json_get_int(o, "popularity", 0);
    m->track_number = audd_json_get_int(o, "track_number", 0);
    m->type = strdup_or_null(audd_json_get_string(o, "type"));
    m->uri = strdup_or_null(audd_json_get_string(o, "uri"));
    m->raw = cJSON_Duplicate(o, 1);
    return m;
}

static void free_spotify(audd_spotify_t *m)
{
    if (m == NULL) return;
    audd_free(m->id);
    audd_free(m->name);
    audd_free(m->type);
    audd_free(m->uri);
    if (m->raw) cJSON_Delete(m->raw);
    audd_free(m);
}

static audd_deezer_t *parse_deezer(const cJSON *o)
{
    if (!cJSON_IsObject(o)) return NULL;
    audd_deezer_t *m = (audd_deezer_t *)audd_malloc(sizeof(*m));
    if (m == NULL) return NULL;
    memset(m, 0, sizeof(*m));
    m->id = audd_json_get_int(o, "id", 0);
    m->title = strdup_or_null(audd_json_get_string(o, "title"));
    m->duration = audd_json_get_int(o, "duration", 0);
    m->link = strdup_or_null(audd_json_get_string(o, "link"));
    m->raw = cJSON_Duplicate(o, 1);
    return m;
}

static void free_deezer(audd_deezer_t *m)
{
    if (m == NULL) return;
    audd_free(m->title);
    audd_free(m->link);
    if (m->raw) cJSON_Delete(m->raw);
    audd_free(m);
}

static audd_napster_t *parse_napster(const cJSON *o)
{
    if (!cJSON_IsObject(o)) return NULL;
    audd_napster_t *m = (audd_napster_t *)audd_malloc(sizeof(*m));
    if (m == NULL) return NULL;
    memset(m, 0, sizeof(*m));
    m->id = strdup_or_null(audd_json_get_string(o, "id"));
    m->name = strdup_or_null(audd_json_get_string(o, "name"));
    m->isrc = strdup_or_null(audd_json_get_string(o, "isrc"));
    m->artist_name = strdup_or_null(audd_json_get_string(o, "artistName"));
    m->album_name = strdup_or_null(audd_json_get_string(o, "albumName"));
    m->raw = cJSON_Duplicate(o, 1);
    return m;
}

static void free_napster(audd_napster_t *m)
{
    if (m == NULL) return;
    audd_free(m->id);
    audd_free(m->name);
    audd_free(m->isrc);
    audd_free(m->artist_name);
    audd_free(m->album_name);
    if (m->raw) cJSON_Delete(m->raw);
    audd_free(m);
}

static int parse_musicbrainz(const cJSON *arr, struct audd_musicbrainz_list *out)
{
    out->items = NULL;
    out->count = 0;
    if (!cJSON_IsArray(arr)) return 0;
    int n = cJSON_GetArraySize(arr);
    if (n <= 0) return 0;
    out->items = (audd_musicbrainz_entry_t *)audd_malloc((size_t)n * sizeof(*out->items));
    if (out->items == NULL) return -1;
    memset(out->items, 0, (size_t)n * sizeof(*out->items));
    for (int i = 0; i < n; ++i) {
        cJSON *it = cJSON_GetArrayItem((cJSON *)arr, i);
        if (!cJSON_IsObject(it)) continue;
        audd_musicbrainz_entry_t *e = &out->items[out->count];
        e->id = strdup_or_null(audd_json_get_string(it, "id"));
        e->title = strdup_or_null(audd_json_get_string(it, "title"));
        e->length = audd_json_get_int(it, "length", 0);
        cJSON *score = cJSON_GetObjectItemCaseSensitive(it, "score");
        if (score != NULL) {
            if (cJSON_IsString(score)) {
                e->score = audd_strdup(score->valuestring);
            } else if (cJSON_IsNumber(score)) {
                e->score = audd_aprintf("%d", (int)score->valuedouble);
            }
        }
        out->count++;
    }
    return 0;
}

static void free_musicbrainz(struct audd_musicbrainz_list *l)
{
    if (l == NULL || l->items == NULL) return;
    for (size_t i = 0; i < l->count; ++i) {
        audd_free(l->items[i].id);
        audd_free(l->items[i].title);
        audd_free(l->items[i].score);
    }
    audd_free(l->items);
    l->items = NULL;
    l->count = 0;
}

/* ---------------- recognition ---------------- */

audd_recognition_t *audd_recognition_from_json(const cJSON *result_obj)
{
    if (!cJSON_IsObject(result_obj)) return NULL;
    audd_recognition_t *r = (audd_recognition_t *)audd_malloc(sizeof(*r));
    if (r == NULL) return NULL;
    memset(r, 0, sizeof(*r));
    r->timecode = strdup_or_null(audd_json_get_string(result_obj, "timecode"));
    r->artist = strdup_or_null(audd_json_get_string(result_obj, "artist"));
    r->title = strdup_or_null(audd_json_get_string(result_obj, "title"));
    r->album = strdup_or_null(audd_json_get_string(result_obj, "album"));
    r->release_date = strdup_or_null(audd_json_get_string(result_obj, "release_date"));
    r->label = strdup_or_null(audd_json_get_string(result_obj, "label"));
    r->song_link = strdup_or_null(audd_json_get_string(result_obj, "song_link"));
    r->isrc = strdup_or_null(audd_json_get_string(result_obj, "isrc"));
    r->upc = strdup_or_null(audd_json_get_string(result_obj, "upc"));

    cJSON *audio_id = cJSON_GetObjectItemCaseSensitive((cJSON *)result_obj, "audio_id");
    if (audio_id != NULL && cJSON_IsNumber(audio_id)) {
        r->has_audio_id = 1;
        r->audio_id = (int)audio_id->valuedouble;
    }

    r->apple_music = parse_apple_music(cJSON_GetObjectItemCaseSensitive((cJSON *)result_obj, "apple_music"));
    r->spotify = parse_spotify(cJSON_GetObjectItemCaseSensitive((cJSON *)result_obj, "spotify"));
    r->deezer = parse_deezer(cJSON_GetObjectItemCaseSensitive((cJSON *)result_obj, "deezer"));
    r->napster = parse_napster(cJSON_GetObjectItemCaseSensitive((cJSON *)result_obj, "napster"));
    parse_musicbrainz(cJSON_GetObjectItemCaseSensitive((cJSON *)result_obj, "musicbrainz"), &r->mb);

    r->raw_obj = cJSON_Duplicate(result_obj, 1);
    if (r->raw_obj != NULL) {
        r->raw_response_str = cJSON_PrintUnformatted(r->raw_obj);
    }
    return r;
}

void audd_recognition_free(audd_recognition_t *r)
{
    if (r == NULL) return;
    audd_free(r->timecode);
    audd_free(r->artist);
    audd_free(r->title);
    audd_free(r->album);
    audd_free(r->release_date);
    audd_free(r->label);
    audd_free(r->song_link);
    audd_free(r->isrc);
    audd_free(r->upc);
    free_apple_music(r->apple_music);
    free_spotify(r->spotify);
    free_deezer(r->deezer);
    free_napster(r->napster);
    free_musicbrainz(&r->mb);
    if (r->raw_obj) cJSON_Delete(r->raw_obj);
    audd_free(r->raw_response_str);
    audd_free(r->thumbnail_url);
    audd_free(r->preview_url);
    for (int i = 0; i < 5; ++i) audd_free(r->streaming_urls[i]);
    audd_free(r);
}

#define SIMPLE_GETTER(retty, name, field) \
    retty audd_recognition_get_##name(const audd_recognition_t *r) \
    { return r ? r->field : NULL; }

SIMPLE_GETTER(const char *, timecode, timecode)
SIMPLE_GETTER(const char *, artist, artist)
SIMPLE_GETTER(const char *, title, title)
SIMPLE_GETTER(const char *, album, album)
SIMPLE_GETTER(const char *, release_date, release_date)
SIMPLE_GETTER(const char *, label, label)
SIMPLE_GETTER(const char *, song_link, song_link)
SIMPLE_GETTER(const char *, isrc, isrc)
SIMPLE_GETTER(const char *, upc, upc)

int audd_recognition_has_audio_id(const audd_recognition_t *r)
{ return r && r->has_audio_id; }
int audd_recognition_get_audio_id(const audd_recognition_t *r)
{ return r ? r->audio_id : 0; }

int audd_recognition_is_custom_match(const audd_recognition_t *r)
{ return r && r->has_audio_id; }

int audd_recognition_is_public_match(const audd_recognition_t *r)
{
    if (r == NULL) return 0;
    if (r->has_audio_id) return 0;
    return (r->artist != NULL && r->artist[0]) || (r->title != NULL && r->title[0]);
}

const char *audd_recognition_raw_response(const audd_recognition_t *r)
{
    return r ? r->raw_response_str : NULL;
}

const char *audd_recognition_extra(const audd_recognition_t *r, const char *key)
{
    /* Returns a borrowed pointer: we cache extras in a small per-recognition
     * map. To keep the API simple we cache lazily on the cJSON tree by
     * mutating a separate struct, but we can also just print on demand and
     * stash the printed form. Here we use a simple approach: print the
     * field, store the pointer in raw_obj's extras map. cJSON has its own
     * memory ownership; we don't want to leak. So we attach a temporary
     * via a side-channel: build a hidden child once. For simplicity, just
     * keep last-extra cache. */
    if (r == NULL || r->raw_obj == NULL || key == NULL) return NULL;
    /* Skip the known typed keys — extras semantics. */
    static const char *known[] = {
        "timecode", "audio_id", "artist", "title", "album", "release_date",
        "label", "song_link", "isrc", "upc",
        "apple_music", "spotify", "deezer", "napster", "musicbrainz", NULL
    };
    for (int i = 0; known[i] != NULL; ++i) {
        if (strcmp(known[i], key) == 0) return NULL;
    }
    /* Walk children and stash the printed form into the cJSON tree as an
     * AUX string (we re-use an "_audd_extra_<key>" sibling). */
    cJSON *it = cJSON_GetObjectItemCaseSensitive(r->raw_obj, key);
    if (it == NULL) return NULL;
    char aux_key[128];
    snprintf(aux_key, sizeof(aux_key), "_audd_extra_%s", key);
    cJSON *cached = cJSON_GetObjectItemCaseSensitive(r->raw_obj, aux_key);
    if (cJSON_IsString(cached) && cached->valuestring != NULL) {
        return cached->valuestring;
    }
    char *printed = cJSON_PrintUnformatted(it);
    if (printed == NULL) return NULL;
    cJSON *added = cJSON_AddStringToObject(r->raw_obj, aux_key, printed);
    audd_free(printed);
    if (added != NULL && cJSON_IsString(added)) {
        return added->valuestring;
    }
    return NULL;
}

/* ---------------- thumbnail / streaming URL helpers ---------------- */

static int song_link_is_lis_tn(const char *url)
{
    return audd_url_hostname_is(url, "lis.tn");
}

static char *append_qparam(const char *url, const char *param)
{
    if (url == NULL || url[0] == '\0') return NULL;
    char sep = (strchr(url, '?') == NULL) ? '?' : '&';
    return audd_aprintf("%s%c%s", url, sep, param);
}

const char *audd_recognition_thumbnail_url(audd_recognition_t *r)
{
    if (r == NULL) return NULL;
    if (r->thumbnail_lazy) return r->thumbnail_url;
    r->thumbnail_lazy = 1;
    if (!song_link_is_lis_tn(r->song_link)) return NULL;
    r->thumbnail_url = append_qparam(r->song_link, "thumb");
    return r->thumbnail_url;
}

static const char *provider_name(audd_provider_t p)
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

static char *direct_streaming_url(const audd_recognition_t *r, audd_provider_t p)
{
    switch (p) {
    case AUDD_PROVIDER_APPLE_MUSIC:
        if (r->apple_music && r->apple_music->url) return audd_strdup(r->apple_music->url);
        break;
    case AUDD_PROVIDER_SPOTIFY:
        if (r->spotify && r->spotify->raw) {
            cJSON *ext = cJSON_GetObjectItemCaseSensitive(r->spotify->raw, "external_urls");
            if (cJSON_IsObject(ext)) {
                const char *u = audd_json_get_string(ext, "spotify");
                if (u) return audd_strdup(u);
            }
            if (r->spotify->uri && r->spotify->uri[0]) return audd_strdup(r->spotify->uri);
        }
        break;
    case AUDD_PROVIDER_DEEZER:
        if (r->deezer && r->deezer->link) return audd_strdup(r->deezer->link);
        break;
    case AUDD_PROVIDER_NAPSTER:
        if (r->napster && r->napster->raw) {
            const char *u = audd_json_get_string(r->napster->raw, "href");
            if (u) return audd_strdup(u);
        }
        break;
    case AUDD_PROVIDER_YOUTUBE:
        break;
    }
    return NULL;
}

const char *audd_recognition_streaming_url(audd_recognition_t *r, audd_provider_t p)
{
    if (r == NULL) return NULL;
    if ((unsigned)p > AUDD_PROVIDER_YOUTUBE) return NULL;
    if (r->streaming_lazy[p]) return r->streaming_urls[p];
    r->streaming_lazy[p] = 1;

    char *direct = direct_streaming_url(r, p);
    if (direct != NULL && direct[0] != '\0') {
        r->streaming_urls[p] = direct;
        return direct;
    }
    audd_free(direct);
    if (song_link_is_lis_tn(r->song_link)) {
        r->streaming_urls[p] = append_qparam(r->song_link, provider_name(p));
        return r->streaming_urls[p];
    }
    return NULL;
}

const char *audd_recognition_preview_url(audd_recognition_t *r)
{
    if (r == NULL) return NULL;
    if (r->preview_lazy) return r->preview_url;
    r->preview_lazy = 1;

    if (r->apple_music && r->apple_music->raw) {
        cJSON *previews = cJSON_GetObjectItemCaseSensitive(r->apple_music->raw, "previews");
        if (cJSON_IsArray(previews)) {
            cJSON *first = cJSON_GetArrayItem(previews, 0);
            if (cJSON_IsObject(first)) {
                const char *u = audd_json_get_string(first, "url");
                if (u && u[0]) {
                    r->preview_url = audd_strdup(u);
                    return r->preview_url;
                }
            }
        }
    }
    if (r->spotify && r->spotify->raw) {
        const char *u = audd_json_get_string(r->spotify->raw, "preview_url");
        if (u && u[0]) {
            r->preview_url = audd_strdup(u);
            return r->preview_url;
        }
    }
    if (r->deezer && r->deezer->raw) {
        const char *u = audd_json_get_string(r->deezer->raw, "preview");
        if (u && u[0]) {
            r->preview_url = audd_strdup(u);
            return r->preview_url;
        }
    }
    return NULL;
}

const audd_apple_music_t *audd_recognition_apple_music(const audd_recognition_t *r) { return r ? r->apple_music : NULL; }
const audd_spotify_t     *audd_recognition_spotify(const audd_recognition_t *r)     { return r ? r->spotify : NULL; }
const audd_deezer_t      *audd_recognition_deezer(const audd_recognition_t *r)      { return r ? r->deezer : NULL; }
const audd_napster_t     *audd_recognition_napster(const audd_recognition_t *r)     { return r ? r->napster : NULL; }

size_t audd_recognition_musicbrainz_count(const audd_recognition_t *r)
{ return r ? r->mb.count : 0; }

const audd_musicbrainz_entry_t *audd_recognition_musicbrainz_at(const audd_recognition_t *r, size_t i)
{
    if (r == NULL || i >= r->mb.count) return NULL;
    return &r->mb.items[i];
}

/* ---------------- block getters ---------------- */

#define BLOCK_GETTER_STR(prefix, type, field) \
    const char *prefix##_get_##field(const type *m) { return m ? m->field : NULL; }
#define BLOCK_GETTER_INT(prefix, type, field) \
    int prefix##_get_##field(const type *m) { return m ? m->field : 0; }

BLOCK_GETTER_STR(audd_apple_music, audd_apple_music_t, artist_name)
BLOCK_GETTER_STR(audd_apple_music, audd_apple_music_t, url)
BLOCK_GETTER_INT(audd_apple_music, audd_apple_music_t, duration_ms)
BLOCK_GETTER_STR(audd_apple_music, audd_apple_music_t, name)
BLOCK_GETTER_STR(audd_apple_music, audd_apple_music_t, isrc)
BLOCK_GETTER_STR(audd_apple_music, audd_apple_music_t, album_name)
BLOCK_GETTER_INT(audd_apple_music, audd_apple_music_t, track_number)
BLOCK_GETTER_STR(audd_apple_music, audd_apple_music_t, release_date)

const char *audd_apple_music_extra(const audd_apple_music_t *m, const char *key)
{
    if (m == NULL || m->raw == NULL) return NULL;
    char aux[128];
    snprintf(aux, sizeof(aux), "_audd_extra_%s", key);
    cJSON *cached = cJSON_GetObjectItemCaseSensitive(m->raw, aux);
    if (cJSON_IsString(cached)) return cached->valuestring;
    cJSON *it = cJSON_GetObjectItemCaseSensitive(m->raw, key);
    if (it == NULL) return NULL;
    char *printed = cJSON_PrintUnformatted(it);
    if (printed == NULL) return NULL;
    cJSON *added = cJSON_AddStringToObject(m->raw, aux, printed);
    audd_free(printed);
    return added ? added->valuestring : NULL;
}

BLOCK_GETTER_STR(audd_spotify, audd_spotify_t, id)
BLOCK_GETTER_STR(audd_spotify, audd_spotify_t, name)
BLOCK_GETTER_INT(audd_spotify, audd_spotify_t, duration_ms)
int audd_spotify_get_explicit(const audd_spotify_t *m) { return m ? m->explicit_ : 0; }
BLOCK_GETTER_INT(audd_spotify, audd_spotify_t, popularity)
BLOCK_GETTER_INT(audd_spotify, audd_spotify_t, track_number)
BLOCK_GETTER_STR(audd_spotify, audd_spotify_t, type)
BLOCK_GETTER_STR(audd_spotify, audd_spotify_t, uri)

const char *audd_spotify_extra(const audd_spotify_t *m, const char *key)
{
    if (m == NULL || m->raw == NULL) return NULL;
    char aux[128]; snprintf(aux, sizeof(aux), "_audd_extra_%s", key);
    cJSON *cached = cJSON_GetObjectItemCaseSensitive(m->raw, aux);
    if (cJSON_IsString(cached)) return cached->valuestring;
    cJSON *it = cJSON_GetObjectItemCaseSensitive(m->raw, key);
    if (it == NULL) return NULL;
    char *p = cJSON_PrintUnformatted(it);
    if (p == NULL) return NULL;
    cJSON *added = cJSON_AddStringToObject(m->raw, aux, p);
    audd_free(p);
    return added ? added->valuestring : NULL;
}

BLOCK_GETTER_INT(audd_deezer, audd_deezer_t, id)
BLOCK_GETTER_STR(audd_deezer, audd_deezer_t, title)
BLOCK_GETTER_INT(audd_deezer, audd_deezer_t, duration)
BLOCK_GETTER_STR(audd_deezer, audd_deezer_t, link)

const char *audd_deezer_extra(const audd_deezer_t *m, const char *key)
{
    if (m == NULL || m->raw == NULL) return NULL;
    char aux[128]; snprintf(aux, sizeof(aux), "_audd_extra_%s", key);
    cJSON *cached = cJSON_GetObjectItemCaseSensitive(m->raw, aux);
    if (cJSON_IsString(cached)) return cached->valuestring;
    cJSON *it = cJSON_GetObjectItemCaseSensitive(m->raw, key);
    if (it == NULL) return NULL;
    char *p = cJSON_PrintUnformatted(it);
    if (p == NULL) return NULL;
    cJSON *added = cJSON_AddStringToObject(m->raw, aux, p);
    audd_free(p);
    return added ? added->valuestring : NULL;
}

BLOCK_GETTER_STR(audd_napster, audd_napster_t, id)
BLOCK_GETTER_STR(audd_napster, audd_napster_t, name)
BLOCK_GETTER_STR(audd_napster, audd_napster_t, isrc)
BLOCK_GETTER_STR(audd_napster, audd_napster_t, artist_name)
BLOCK_GETTER_STR(audd_napster, audd_napster_t, album_name)

BLOCK_GETTER_STR(audd_musicbrainz, audd_musicbrainz_entry_t, id)
BLOCK_GETTER_STR(audd_musicbrainz, audd_musicbrainz_entry_t, title)
BLOCK_GETTER_INT(audd_musicbrainz, audd_musicbrainz_entry_t, length)
BLOCK_GETTER_STR(audd_musicbrainz, audd_musicbrainz_entry_t, score)

/* ============================================================== *
 * Recognition: HTTP request                                        *
 * ============================================================== */

typedef struct {
    const char *url;          /* URL source (NULL if not URL) */
    const char *file_path;    /* file path source (NULL if not path) */
    const void *bytes;        /* in-memory bytes (NULL if not bytes) */
    size_t      bytes_size;
    /* options */
    const char *return_csv;   /* heap or NULL */
    const char *market;
    const char **extra_parameters; /* NULL-terminated key,value,...,NULL */
    /* enterprise extras */
    const audd_enterprise_options_t *eopts;
    /* shared */
    int        is_enterprise;
} recognize_ctx_t;

static int recognize_attempt(audd_client_t *client,
                             audd_http_response_t *resp,
                             int *body_was_uploaded,
                             void *ud)
{
    recognize_ctx_t *ctx = (recognize_ctx_t *)ud;
    /* Count extras and allocate field array with margin for typed fields. */
    size_t extras_pairs = 0;
    if (ctx->extra_parameters) {
        for (size_t i = 0; ctx->extra_parameters[i] != NULL && ctx->extra_parameters[i + 1] != NULL; i += 2) {
            extras_pairs++;
        }
    }
    const size_t typed_slots = 32; /* upper bound for typed fields below */
    const size_t total_slots = typed_slots + extras_pairs * 2 + 1;
    const char **fields = (const char **)audd_malloc(total_slots * sizeof(*fields));
    if (fields == NULL) return -1;
    memset(fields, 0, total_slots * sizeof(*fields));
    size_t f = 0;
    char *return_str = NULL;
    char skip_buf[16], every_buf[16], limit_buf[16], skipfs_buf[16];
    audd_http_file_t file = {0};
    audd_http_form_t form = {0};

    /* extras first; typed params win on collision (later writes shadow earlier) */
    if (ctx->extra_parameters) {
        for (size_t i = 0; ctx->extra_parameters[i] != NULL && ctx->extra_parameters[i + 1] != NULL; i += 2) {
            fields[f++] = ctx->extra_parameters[i];
            fields[f++] = ctx->extra_parameters[i + 1];
        }
    }

    if (ctx->is_enterprise && ctx->eopts) {
        if (ctx->eopts->skip >= 0) {
            snprintf(skip_buf, sizeof(skip_buf), "%d", ctx->eopts->skip);
            fields[f++] = "skip"; fields[f++] = skip_buf;
        }
        if (ctx->eopts->every >= 0) {
            snprintf(every_buf, sizeof(every_buf), "%d", ctx->eopts->every);
            fields[f++] = "every"; fields[f++] = every_buf;
        }
        if (ctx->eopts->limit >= 0) {
            snprintf(limit_buf, sizeof(limit_buf), "%d", ctx->eopts->limit);
            fields[f++] = "limit"; fields[f++] = limit_buf;
        }
        if (ctx->eopts->skip_first_seconds >= 0) {
            snprintf(skipfs_buf, sizeof(skipfs_buf), "%d", ctx->eopts->skip_first_seconds);
            fields[f++] = "skip_first_seconds"; fields[f++] = skipfs_buf;
        }
        if (ctx->eopts->use_timecode >= 0) {
            fields[f++] = "use_timecode";
            fields[f++] = ctx->eopts->use_timecode ? "true" : "false";
        }
        if (ctx->eopts->accurate_offsets >= 0) {
            fields[f++] = "accurate_offsets";
            fields[f++] = ctx->eopts->accurate_offsets ? "true" : "false";
        }
    }
    if (ctx->return_csv && ctx->return_csv[0]) {
        fields[f++] = "return"; fields[f++] = ctx->return_csv;
    }
    if (ctx->market && ctx->market[0]) {
        fields[f++] = "market"; fields[f++] = ctx->market;
    }
    if (ctx->url) {
        fields[f++] = "url"; fields[f++] = ctx->url;
    }
    fields[f] = NULL;
    form.fields = fields;

    if (ctx->file_path) {
        const char *base = strrchr(ctx->file_path, '/');
        file.name = "file";
        file.filename = base ? base + 1 : ctx->file_path;
        file.content_type = "application/octet-stream";
        file.path = ctx->file_path;
        form.file = &file;
    } else if (ctx->bytes) {
        file.name = "file";
        file.filename = "upload.bin";
        file.content_type = "application/octet-stream";
        file.data = ctx->bytes;
        file.size = ctx->bytes_size;
        form.file = &file;
    }

    long timeout = ctx->is_enterprise
        ? client->options.enterprise_timeout_seconds
        : client->options.standard_timeout_seconds;
    const char *endpoint = ctx->is_enterprise
        ? "https://enterprise.audd.io/"
        : "https://api.audd.io/";

    int rc = audd_http_post(client, endpoint, &form, timeout, resp, body_was_uploaded);
    audd_free(return_str);
    audd_free((void *)fields);
    return rc;
}

/* Build comma-CSV of providers; caller frees. NULL when empty list. */
static char *join_csv(const char **arr)
{
    if (arr == NULL || arr[0] == NULL) return NULL;
    size_t total = 0, count = 0;
    for (size_t i = 0; arr[i] != NULL; ++i) {
        if (arr[i][0] == '\0') continue;
        total += strlen(arr[i]) + 1;
        ++count;
    }
    if (count == 0) return NULL;
    char *out = (char *)audd_malloc(total);
    if (out == NULL) return NULL;
    size_t off = 0;
    int first = 1;
    for (size_t i = 0; arr[i] != NULL; ++i) {
        if (arr[i][0] == '\0') continue;
        if (!first) out[off++] = ',';
        first = 0;
        size_t l = strlen(arr[i]);
        memcpy(out + off, arr[i], l);
        off += l;
    }
    out[off] = '\0';
    return out;
}

audd_enterprise_options_t audd_enterprise_options_default(void)
{
    audd_enterprise_options_t o;
    o.return_metadata = NULL;
    o.skip = -1;
    o.every = -1;
    o.limit = -1;
    o.skip_first_seconds = -1;
    o.use_timecode = -1;
    o.accurate_offsets = 1; /* on by default; set 0 to opt out */
    o.extra_parameters = NULL;
    return o;
}

static int file_exists(const char *path)
{
    struct stat st;
    return path != NULL && stat(path, &st) == 0 && !S_ISDIR(st.st_mode);
}

static audd_error_t do_recognize_common(audd_client_t *client,
                                        const char *source_url_or_path,
                                        const void *bytes, size_t bytes_size,
                                        const audd_recognize_options_t *r_opts,
                                        const audd_enterprise_options_t *e_opts,
                                        int is_enterprise,
                                        audd_recognition_t **out_recognition,
                                        audd_enterprise_result_t **out_enterprise)
{
    if (client == NULL) return AUDD_ERR_INVALID_ARGUMENT;
    audd_client_clear_error(client);

    recognize_ctx_t ctx = {0};
    ctx.is_enterprise = is_enterprise;
    char *return_csv = NULL;
    /* When no enterprise options are supplied, fall back to the defaults so
     * accurate offsets are still requested (precise start_seconds/end_seconds).
     * Callers opt out by passing options with accurate_offsets = 0. */
    audd_enterprise_options_t e_defaults;
    if (is_enterprise && e_opts == NULL) {
        e_defaults = audd_enterprise_options_default();
        e_opts = &e_defaults;
    }
    if (is_enterprise && e_opts) {
        return_csv = join_csv(e_opts->return_metadata);
        ctx.eopts = e_opts;
        ctx.extra_parameters = e_opts->extra_parameters;
    } else if (!is_enterprise && r_opts) {
        return_csv = join_csv(r_opts->return_metadata);
        ctx.market = r_opts->market;
        ctx.extra_parameters = r_opts->extra_parameters;
    }
    ctx.return_csv = return_csv;

    if (bytes != NULL) {
        ctx.bytes = bytes;
        ctx.bytes_size = bytes_size;
    } else if (source_url_or_path != NULL) {
        if (audd_url_is_http(source_url_or_path)) {
            ctx.url = source_url_or_path;
        } else if (file_exists(source_url_or_path)) {
            ctx.file_path = source_url_or_path;
        } else {
            char *m = audd_aprintf(
                "audd: %.256s is not an HTTP URL (must start with http:// or https://) "
                "and is not an existing file path", source_url_or_path);
            audd_client_set_error(client, m ? m : "invalid source", 0);
            audd_free(m);
            audd_free(return_csv);
            return AUDD_ERR_INVALID_ARGUMENT;
        }
    } else {
        audd_client_set_error(client, "audd: source must not be NULL", 0);
        audd_free(return_csv);
        return AUDD_ERR_INVALID_ARGUMENT;
    }

    audd_http_response_t resp = {0};
    int rc = audd_retry_do(client, AUDD_RETRY_RECOGNITION,
                            recognize_attempt, &ctx, &resp);
    audd_free(return_csv);
    if (rc != 0) {
        audd_http_response_free(&resp);
        return AUDD_ERR_CONNECTION;
    }
    audd_error_t derr = audd_decode_or_raise(client, &resp, 0);
    if (derr != AUDD_OK) {
        audd_http_response_free(&resp);
        return derr;
    }
    cJSON *result = cJSON_GetObjectItemCaseSensitive(resp.json, "result");
    if (is_enterprise) {
        *out_enterprise = audd_enterprise_from_json(result);
    } else {
        if (result == NULL || cJSON_IsNull(result)) {
            *out_recognition = NULL;
        } else {
            *out_recognition = audd_recognition_from_json(result);
            if (*out_recognition == NULL) {
                audd_client_set_error(client, "out of memory", 0);
                audd_http_response_free(&resp);
                return AUDD_ERR_OUT_OF_MEMORY;
            }
        }
    }
    audd_http_response_free(&resp);
    return AUDD_OK;
}

audd_error_t audd_recognize(audd_client_t *client,
                             const char *source,
                             const audd_recognize_options_t *options,
                             audd_recognition_t **out_result)
{
    if (out_result == NULL) return AUDD_ERR_INVALID_ARGUMENT;
    *out_result = NULL;
    return do_recognize_common(client, source, NULL, 0, options, NULL, 0, out_result, NULL);
}

audd_error_t audd_recognize_bytes(audd_client_t *client,
                                   const void *data, size_t size,
                                   const audd_recognize_options_t *options,
                                   audd_recognition_t **out_result)
{
    if (out_result == NULL) return AUDD_ERR_INVALID_ARGUMENT;
    *out_result = NULL;
    if (data == NULL || size == 0) {
        return AUDD_ERR_INVALID_ARGUMENT;
    }
    return do_recognize_common(client, NULL, data, size, options, NULL, 0, out_result, NULL);
}

audd_error_t audd_recognize_enterprise(audd_client_t *client,
                                        const char *source,
                                        const audd_enterprise_options_t *options,
                                        audd_enterprise_result_t **out_result)
{
    if (out_result == NULL) return AUDD_ERR_INVALID_ARGUMENT;
    *out_result = NULL;
    return do_recognize_common(client, source, NULL, 0, NULL, options, 1, NULL, out_result);
}

audd_error_t audd_recognize_enterprise_bytes(audd_client_t *client,
                                              const void *data, size_t size,
                                              const audd_enterprise_options_t *options,
                                              audd_enterprise_result_t **out_result)
{
    if (out_result == NULL) return AUDD_ERR_INVALID_ARGUMENT;
    *out_result = NULL;
    if (data == NULL || size == 0) return AUDD_ERR_INVALID_ARGUMENT;
    return do_recognize_common(client, NULL, data, size, NULL, options, 1, NULL, out_result);
}

/* Suppress unused-param warning when kApiBase/kEnterpriseBase aren't used. */
__attribute__((unused)) static const char *kApiBaseRef = "https://api.audd.io";
__attribute__((unused)) static const char *kEntBaseRef = "https://enterprise.audd.io";

/* Reference the `api_token` field by indirection so static checkers don't
 * complain about kApiBase / kEnterpriseBase being unused from this file. */
__attribute__((unused)) static void _client_keepalive(audd_client_t *c) { (void)c; (void)kApiBase; (void)kEnterpriseBase; }
