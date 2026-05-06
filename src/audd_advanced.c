/* audd_advanced.c — find_lyrics + raw_request escape hatch. */
#include "audd_internal.h"
#include "audd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../vendor/cJSON/cJSON.h"

struct audd_lyrics_result {
    char *artist;
    char *title;
    char *lyrics;
    int   song_id;
    char *media;
    char *full_title;
    int   artist_id;
    char *song_link;
};

struct audd_lyrics_list {
    audd_lyrics_result_t *items;
    size_t count;
};

static char *sdup(const char *s) { return s ? audd_strdup(s) : NULL; }

typedef struct {
    const char *method;
    const char **kv;
} adv_ctx_t;

static int adv_attempt(audd_client_t *client,
                        audd_http_response_t *resp,
                        int *body_was_uploaded,
                        void *ud)
{
    adv_ctx_t *ctx = (adv_ctx_t *)ud;
    char *url = audd_aprintf("https://api.audd.io/%s/", ctx->method);
    if (url == NULL) return -1;
    audd_http_form_t form = {0};
    form.fields = ctx->kv;
    int rc = audd_http_post(client, url, &form,
                             client->options.standard_timeout_seconds,
                             resp, body_was_uploaded);
    audd_free(url);
    return rc;
}

audd_error_t audd_advanced_find_lyrics(audd_client_t *client,
                                        const char *query,
                                        audd_lyrics_list_t **out_results)
{
    if (client == NULL || query == NULL || out_results == NULL) return AUDD_ERR_INVALID_ARGUMENT;
    *out_results = NULL;
    const char *kv[] = { "q", query, NULL };
    adv_ctx_t ctx = { "findLyrics", kv };
    audd_http_response_t resp = {0};
    int rc = audd_retry_do(client, AUDD_RETRY_RECOGNITION, adv_attempt, &ctx, &resp);
    if (rc != 0) { audd_http_response_free(&resp); return AUDD_ERR_CONNECTION; }
    audd_error_t e = audd_decode_or_raise(client, &resp, 0);
    if (e != AUDD_OK) { audd_http_response_free(&resp); return e; }

    audd_lyrics_list_t *list = (audd_lyrics_list_t *)audd_malloc(sizeof(*list));
    if (list == NULL) { audd_http_response_free(&resp); return AUDD_ERR_OUT_OF_MEMORY; }
    memset(list, 0, sizeof(*list));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(resp.json, "result");
    if (cJSON_IsArray(result)) {
        int n = cJSON_GetArraySize(result);
        if (n > 0) {
            list->items = (audd_lyrics_result_t *)audd_malloc((size_t)n * sizeof(*list->items));
            if (list->items == NULL) {
                audd_free(list);
                audd_http_response_free(&resp);
                return AUDD_ERR_OUT_OF_MEMORY;
            }
            memset(list->items, 0, (size_t)n * sizeof(*list->items));
            for (int i = 0; i < n; ++i) {
                cJSON *it = cJSON_GetArrayItem(result, i);
                if (!cJSON_IsObject(it)) continue;
                audd_lyrics_result_t *l = &list->items[list->count];
                l->artist = sdup(audd_json_get_string(it, "artist"));
                l->title = sdup(audd_json_get_string(it, "title"));
                l->lyrics = sdup(audd_json_get_string(it, "lyrics"));
                l->song_id = audd_json_get_int(it, "song_id", 0);
                l->media = sdup(audd_json_get_string(it, "media"));
                l->full_title = sdup(audd_json_get_string(it, "full_title"));
                l->artist_id = audd_json_get_int(it, "artist_id", 0);
                l->song_link = sdup(audd_json_get_string(it, "song_link"));
                list->count++;
            }
        }
    }
    audd_http_response_free(&resp);
    *out_results = list;
    return AUDD_OK;
}

void audd_lyrics_list_free(audd_lyrics_list_t *list)
{
    if (list == NULL) return;
    for (size_t i = 0; i < list->count; ++i) {
        audd_free(list->items[i].artist);
        audd_free(list->items[i].title);
        audd_free(list->items[i].lyrics);
        audd_free(list->items[i].media);
        audd_free(list->items[i].full_title);
        audd_free(list->items[i].song_link);
    }
    audd_free(list->items);
    audd_free(list);
}

size_t audd_lyrics_list_count(const audd_lyrics_list_t *list) { return list ? list->count : 0; }
const audd_lyrics_result_t *audd_lyrics_list_at(const audd_lyrics_list_t *list, size_t i)
{
    if (list == NULL || i >= list->count) return NULL;
    return &list->items[i];
}

#define LGET_STR(field) const char *audd_lyrics_get_##field(const audd_lyrics_result_t *l) { return l ? l->field : NULL; }
#define LGET_INT(field) int audd_lyrics_get_##field(const audd_lyrics_result_t *l) { return l ? l->field : 0; }
LGET_STR(artist)
LGET_STR(title)
LGET_STR(lyrics)
LGET_INT(song_id)
LGET_STR(media)
LGET_STR(full_title)
LGET_INT(artist_id)
LGET_STR(song_link)

audd_error_t audd_advanced_raw_request(audd_client_t *client,
                                        const char *method,
                                        const char **params_kv,
                                        char **out_response)
{
    if (client == NULL || method == NULL || out_response == NULL) return AUDD_ERR_INVALID_ARGUMENT;
    *out_response = NULL;
    adv_ctx_t ctx = { method, params_kv };
    audd_http_response_t resp = {0};
    int rc = audd_retry_do(client, AUDD_RETRY_RECOGNITION, adv_attempt, &ctx, &resp);
    if (rc != 0) { audd_http_response_free(&resp); return AUDD_ERR_CONNECTION; }
    audd_error_t e = audd_decode_top_level(client, &resp);
    if (e != AUDD_OK) { audd_http_response_free(&resp); return e; }
    if (resp.body) {
        *out_response = audd_strndup(resp.body, resp.body_len);
        if (*out_response == NULL) { audd_http_response_free(&resp); return AUDD_ERR_OUT_OF_MEMORY; }
    }
    audd_http_response_free(&resp);
    return AUDD_OK;
}
