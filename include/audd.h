/*
 * audd.h — Official C SDK for the AudD music recognition API.
 *
 * Identify music from a short audio clip, a long audio file, or a live
 * stream. See https://docs.audd.io for the underlying HTTP API.
 *
 * Copyright (c) 2026 AudD, LLC (https://audd.io). Licensed under the MIT license. See LICENSE.
 */
#ifndef AUDD_H
#define AUDD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(AUDD_BUILDING_DLL)
#    define AUDD_API __declspec(dllexport)
#  elif defined(AUDD_USING_DLL)
#    define AUDD_API __declspec(dllimport)
#  else
#    define AUDD_API
#  endif
#else
#  if defined(__GNUC__) && (__GNUC__ >= 4)
#    define AUDD_API __attribute__((visibility("default")))
#  else
#    define AUDD_API
#  endif
#endif

/* ---------------------------------------------------------------------- *
 * Version and SDK metadata                                                *
 * ---------------------------------------------------------------------- */

/** SDK semantic version string, e.g. "1.5.2". */
AUDD_API const char *audd_version(void);

/* ---------------------------------------------------------------------- *
 * Allocator hook                                                          *
 * ---------------------------------------------------------------------- *
 *
 * Embedders can plug in their own allocator (arena, tracking, custom
 * pool). Defaults to libc malloc / free / realloc. Setting the allocator
 * is NOT thread-safe; do it once at process start before any other audd_*
 * call. Allocator functions must follow C99 semantics: malloc(0) may
 * return NULL or a unique pointer, free(NULL) is a no-op, realloc(p, 0)
 * is implementation-defined.
 */

typedef struct {
    void *(*malloc_fn)(size_t size);
    void  (*free_fn)(void *ptr);
    void *(*realloc_fn)(void *ptr, size_t size);
} audd_allocator_t;

/** Replace the global allocator. Pass NULL to restore libc defaults. */
AUDD_API void audd_set_allocator(const audd_allocator_t *alloc);

/* ---------------------------------------------------------------------- *
 * Error model                                                             *
 * ---------------------------------------------------------------------- */

/**
 * Error sentinel returned from every fallible audd_* call.
 *
 * Mirrors audd-go's sentinel errors: callers can switch on the category
 * without parsing strings. Use audd_last_error_message(client) /
 * audd_last_error_code(client) for the human-readable detail and the
 * AudD numeric error code (e.g. 901, 904).
 */
typedef enum {
    AUDD_OK                          = 0,
    AUDD_ERR_AUTHENTICATION          = 1,  /* codes 900 / 901 / 903    */
    AUDD_ERR_QUOTA                   = 2,  /* code 902                 */
    AUDD_ERR_SUBSCRIPTION            = 3,  /* codes 904 / 905          */
    AUDD_ERR_CUSTOM_CATALOG_ACCESS   = 4,  /* 904/905 in catalog ctx   */
    AUDD_ERR_INVALID_REQUEST         = 5,  /* 50 / 51 / 6xx / 7xx / 906 */
    AUDD_ERR_INVALID_AUDIO           = 6,  /* 300 / 400 / 500          */
    AUDD_ERR_RATE_LIMIT              = 7,  /* 611                      */
    AUDD_ERR_STREAM_LIMIT            = 8,  /* 610                      */
    AUDD_ERR_NOT_RELEASED            = 9,  /* 907                      */
    AUDD_ERR_BLOCKED                 = 10, /* 19 / 31337               */
    AUDD_ERR_NEEDS_UPDATE            = 11, /* 20                       */
    AUDD_ERR_SERVER                  = 12, /* 5xx, non-JSON gateway    */
    AUDD_ERR_CONNECTION              = 13, /* DNS / TCP / TLS / timeout */
    AUDD_ERR_SERIALIZATION           = 14, /* 2xx with unparseable JSON */
    AUDD_ERR_INVALID_ARGUMENT        = 15, /* SDK-side: bad arg        */
    AUDD_ERR_OUT_OF_MEMORY           = 16,
    AUDD_ERR_IO                      = 17, /* file open/read failure   */
    AUDD_ERR_CLOSED                  = 18, /* client closed             */
    AUDD_ERR_NOT_FOUND               = 19  /* no result for query      */
} audd_error_t;

/** Stable text for an error sentinel. Never NULL. */
AUDD_API const char *audd_error_string(audd_error_t err);

/* ---------------------------------------------------------------------- *
 * Streaming-provider enum                                                  *
 * ---------------------------------------------------------------------- */

typedef enum {
    AUDD_PROVIDER_SPOTIFY     = 0,
    AUDD_PROVIDER_APPLE_MUSIC = 1,
    AUDD_PROVIDER_DEEZER      = 2,
    AUDD_PROVIDER_NAPSTER     = 3,
    AUDD_PROVIDER_YOUTUBE     = 4
} audd_provider_t;

/* ---------------------------------------------------------------------- *
 * Opaque handles                                                           *
 * ---------------------------------------------------------------------- */

/** AudD client. Construct with audd_client_new. */
typedef struct audd_client audd_client_t;

/** Result of a single recognize call. Owns its returned strings. */
typedef struct audd_recognition audd_recognition_t;

/** Result of a recognizeEnterprise call: a list of matches. */
typedef struct audd_enterprise_result audd_enterprise_result_t;

/** One match within an enterprise result. Borrowed from the parent result. */
typedef struct audd_enterprise_match audd_enterprise_match_t;

/** Stream descriptor returned by audd_streams_list. */
typedef struct audd_stream audd_stream_t;

/** Vector of audd_stream_t entries. Owns its items. */
typedef struct audd_stream_list audd_stream_list_t;

/** Parsed stream-callback recognition match. */
typedef struct audd_stream_callback_match audd_stream_callback_match_t;

/** One song candidate inside a stream-callback match. */
typedef struct audd_stream_callback_song audd_stream_callback_song_t;

/** Parsed stream-callback notification (lifecycle event). */
typedef struct audd_stream_callback_notification audd_stream_callback_notification_t;

/** Result of audd_advanced_find_lyrics: vector of LyricsResult entries. */
typedef struct audd_lyrics_list audd_lyrics_list_t;
typedef struct audd_lyrics_result audd_lyrics_result_t;

/** Active longpoll handle. See audd_longpoll_run / audd_longpoll_close. */
typedef struct audd_longpoll audd_longpoll_t;

/* ---------------------------------------------------------------------- *
 * Client lifecycle                                                         *
 * ---------------------------------------------------------------------- */

typedef struct {
    /** Per-request timeout for the standard endpoint. Default: 60s. */
    long standard_timeout_seconds;
    /** Per-request timeout for the enterprise endpoint. Default: 7200s. */
    long enterprise_timeout_seconds;
    /** Maximum retry attempts (incl. first try). Default: 3. */
    int max_attempts;
    /** Initial backoff in milliseconds (geometric). Default: 500ms. */
    long backoff_ms;
    /** Optional User-Agent suffix appended after "audd-c/<ver>". */
    const char *user_agent_suffix;
    /** Optional CA bundle path for libcurl (overrides system default). */
    const char *ca_bundle_path;
} audd_options_t;

/** Sane defaults for audd_options_t. */
AUDD_API audd_options_t audd_options_default(void);

/**
 * Build a client with the given API token and options.
 *
 * If api_token is NULL or empty, the SDK reads AUDD_API_TOKEN from the
 * environment. If that's also unset, the client is still constructed; the
 * first network call returns AUDD_ERR_AUTHENTICATION.
 *
 * options may be NULL (uses audd_options_default()). The options struct
 * is copied.
 *
 * Thread-safety: audd_client_t is safe to share across threads. A
 * recognize / longpoll call running on one thread can be cancelled from
 * another via audd_client_close (terminal) or audd_longpoll_close (per
 * subscription).
 *
 * Returns NULL only on out-of-memory.
 */
AUDD_API audd_client_t *audd_client_new(const char *api_token,
                                        const audd_options_t *options);

/** Like audd_client_new, but writes AUDD_ERR_AUTHENTICATION when no token
 *  is configured. Returns NULL when err is set, else a fresh client. */
AUDD_API audd_client_t *audd_client_new_strict(const char *api_token,
                                               const audd_options_t *options,
                                               audd_error_t *err);

/** Free the client and all resources. NULL-safe. */
AUDD_API void audd_client_free(audd_client_t *client);

/** Atomically rotate the API token. Returns AUDD_ERR_INVALID_ARGUMENT for
 *  NULL/empty inputs. In-flight requests continue with the old token. */
AUDD_API audd_error_t audd_client_set_api_token(audd_client_t *client,
                                                const char *new_token);

/** Borrowed pointer to the in-effect API token. NULL when none set. */
AUDD_API const char *audd_client_api_token(const audd_client_t *client);

/** Last error message produced by `client`. Never NULL; "" when no error. */
AUDD_API const char *audd_last_error_message(const audd_client_t *client);

/** AudD numeric error code from the most recent failed call (e.g. 901).
 *  0 when no error or when failure was SDK-local (not from the API). */
AUDD_API int audd_last_error_code(const audd_client_t *client);

/** X-Request-Id of the most recent response, if any. Borrowed. "" if none. */
AUDD_API const char *audd_last_request_id(const audd_client_t *client);

/* ---------------------------------------------------------------------- *
 * Source helper                                                            *
 * ---------------------------------------------------------------------- *
 *
 * Callers identify audio with one of three source types:
 *   - URL: starts with "http://" or "https://"
 *   - File path: an existing path on disk
 *   - In-memory bytes
 *
 * audd_recognize() takes a single `source` C-string that auto-detects URL
 * vs. file path. Use audd_recognize_bytes() for in-memory data.
 */

/* ---------------------------------------------------------------------- *
 * Recognition (short clip)                                                 *
 * ---------------------------------------------------------------------- */

typedef struct {
    /** NULL-terminated array of strings; e.g. {"apple_music","spotify",NULL}.
     *  Pass NULL or {NULL} for the default lean response. */
    const char **return_metadata;
    /** ISO country market code. NULL → server default ("us"). */
    const char *market;
} audd_recognize_options_t;

/**
 * Recognize a short audio clip from a URL or local file path.
 *
 * `source` must be a URL (http:// or https://) or an existing file path.
 *
 * `out_result` is set to a freshly allocated audd_recognition_t* on a
 * positive match (caller frees with audd_recognition_free), to NULL on a
 * "no match" success response, or unchanged on error. err return value
 * disambiguates the three cases:
 *   - AUDD_OK + *out_result != NULL  → match
 *   - AUDD_OK + *out_result == NULL  → no match
 *   - AUDD_ERR_*                     → error (use audd_last_error_message)
 *
 * options may be NULL (defaults). Thread-safety: safe to call concurrently
 * with itself; thread-unsafe with audd_client_set_api_token (which is fine
 * because token rotation is rare).
 */
AUDD_API audd_error_t audd_recognize(audd_client_t *client,
                                     const char *source,
                                     const audd_recognize_options_t *options,
                                     audd_recognition_t **out_result);

/** Recognize from in-memory bytes. The buffer is copied; safe to free
 *  after the call returns. Same out_result semantics as audd_recognize. */
AUDD_API audd_error_t audd_recognize_bytes(audd_client_t *client,
                                           const void *data,
                                           size_t size,
                                           const audd_recognize_options_t *options,
                                           audd_recognition_t **out_result);

/** Free a recognition result. NULL-safe. */
AUDD_API void audd_recognition_free(audd_recognition_t *r);

/* Recognition getters: returned strings are owned by `r` — copy if needed
 * to outlive the result. NULL is returned for absent fields. */

AUDD_API const char *audd_recognition_get_timecode(const audd_recognition_t *r);
AUDD_API const char *audd_recognition_get_artist(const audd_recognition_t *r);
AUDD_API const char *audd_recognition_get_title(const audd_recognition_t *r);
AUDD_API const char *audd_recognition_get_album(const audd_recognition_t *r);
AUDD_API const char *audd_recognition_get_release_date(const audd_recognition_t *r);
AUDD_API const char *audd_recognition_get_label(const audd_recognition_t *r);
AUDD_API const char *audd_recognition_get_song_link(const audd_recognition_t *r);
AUDD_API const char *audd_recognition_get_isrc(const audd_recognition_t *r);
AUDD_API const char *audd_recognition_get_upc(const audd_recognition_t *r);

/** Custom-DB matches set audio_id; public-DB matches set artist/title.
 *  audd_recognition_has_audio_id reports whether audio_id was returned.
 *  audd_recognition_get_audio_id returns its int value (0 when absent). */
AUDD_API int audd_recognition_has_audio_id(const audd_recognition_t *r);
AUDD_API int audd_recognition_get_audio_id(const audd_recognition_t *r);
AUDD_API int audd_recognition_is_public_match(const audd_recognition_t *r);
AUDD_API int audd_recognition_is_custom_match(const audd_recognition_t *r);

/** Full unparsed JSON for the result block. Owned by `r`. */
AUDD_API const char *audd_recognition_raw_response(const audd_recognition_t *r);

/** Read an unknown / undocumented field from the result by JSON key. The
 *  returned string is the field's raw JSON (string-quoted, number, or
 *  object/array source). NULL when key not present. Owned by `r`. */
AUDD_API const char *audd_recognition_extra(const audd_recognition_t *r,
                                            const char *key);

/** Cover-art URL for lis.tn-hosted song_links; NULL otherwise. Owned by `r`. */
AUDD_API const char *audd_recognition_thumbnail_url(audd_recognition_t *r);

/** lis.tn redirect (or direct provider URL when metadata block is requested)
 *  for the named provider. NULL when no resolvable URL. Owned by `r`. */
AUDD_API const char *audd_recognition_streaming_url(audd_recognition_t *r,
                                                    audd_provider_t provider);

/** First available preview URL across requested providers (apple_music,
 *  spotify, deezer in priority order). NULL if none. Owned by `r`. */
AUDD_API const char *audd_recognition_preview_url(audd_recognition_t *r);

/* Optional metadata blocks. Pointers may be NULL. Each block is owned by
 * `r`. Sub-getters return NULL / 0 for absent fields. */

typedef struct audd_apple_music audd_apple_music_t;
typedef struct audd_spotify audd_spotify_t;
typedef struct audd_deezer audd_deezer_t;
typedef struct audd_napster audd_napster_t;
typedef struct audd_musicbrainz_entry audd_musicbrainz_entry_t;

AUDD_API const audd_apple_music_t *audd_recognition_apple_music(const audd_recognition_t *r);
AUDD_API const audd_spotify_t     *audd_recognition_spotify(const audd_recognition_t *r);
AUDD_API const audd_deezer_t      *audd_recognition_deezer(const audd_recognition_t *r);
AUDD_API const audd_napster_t     *audd_recognition_napster(const audd_recognition_t *r);
AUDD_API size_t                    audd_recognition_musicbrainz_count(const audd_recognition_t *r);
AUDD_API const audd_musicbrainz_entry_t *audd_recognition_musicbrainz_at(const audd_recognition_t *r, size_t i);

/* Apple Music block getters. */
AUDD_API const char *audd_apple_music_get_artist_name(const audd_apple_music_t *m);
AUDD_API const char *audd_apple_music_get_url(const audd_apple_music_t *m);
AUDD_API int         audd_apple_music_get_duration_ms(const audd_apple_music_t *m);
AUDD_API const char *audd_apple_music_get_name(const audd_apple_music_t *m);
AUDD_API const char *audd_apple_music_get_isrc(const audd_apple_music_t *m);
AUDD_API const char *audd_apple_music_get_album_name(const audd_apple_music_t *m);
AUDD_API int         audd_apple_music_get_track_number(const audd_apple_music_t *m);
AUDD_API const char *audd_apple_music_get_release_date(const audd_apple_music_t *m);
AUDD_API const char *audd_apple_music_extra(const audd_apple_music_t *m, const char *key);

/* Spotify block getters. */
AUDD_API const char *audd_spotify_get_id(const audd_spotify_t *m);
AUDD_API const char *audd_spotify_get_name(const audd_spotify_t *m);
AUDD_API int         audd_spotify_get_duration_ms(const audd_spotify_t *m);
AUDD_API int         audd_spotify_get_explicit(const audd_spotify_t *m);
AUDD_API int         audd_spotify_get_popularity(const audd_spotify_t *m);
AUDD_API int         audd_spotify_get_track_number(const audd_spotify_t *m);
AUDD_API const char *audd_spotify_get_type(const audd_spotify_t *m);
AUDD_API const char *audd_spotify_get_uri(const audd_spotify_t *m);
AUDD_API const char *audd_spotify_extra(const audd_spotify_t *m, const char *key);

/* Deezer block getters. */
AUDD_API int         audd_deezer_get_id(const audd_deezer_t *m);
AUDD_API const char *audd_deezer_get_title(const audd_deezer_t *m);
AUDD_API int         audd_deezer_get_duration(const audd_deezer_t *m);
AUDD_API const char *audd_deezer_get_link(const audd_deezer_t *m);
AUDD_API const char *audd_deezer_extra(const audd_deezer_t *m, const char *key);

/* Napster block getters. */
AUDD_API const char *audd_napster_get_id(const audd_napster_t *m);
AUDD_API const char *audd_napster_get_name(const audd_napster_t *m);
AUDD_API const char *audd_napster_get_isrc(const audd_napster_t *m);
AUDD_API const char *audd_napster_get_artist_name(const audd_napster_t *m);
AUDD_API const char *audd_napster_get_album_name(const audd_napster_t *m);

/* MusicBrainz entry getters. */
AUDD_API const char *audd_musicbrainz_get_id(const audd_musicbrainz_entry_t *m);
AUDD_API const char *audd_musicbrainz_get_title(const audd_musicbrainz_entry_t *m);
AUDD_API int         audd_musicbrainz_get_length(const audd_musicbrainz_entry_t *m);
AUDD_API const char *audd_musicbrainz_get_score(const audd_musicbrainz_entry_t *m);

/* ---------------------------------------------------------------------- *
 * Recognition (long file — enterprise)                                     *
 * ---------------------------------------------------------------------- */

typedef struct {
    const char **return_metadata;
    /** Optional: -1 means "not set"; otherwise sent verbatim. */
    int skip;
    int every;
    int limit;
    int skip_first_seconds;
    /** Tri-state: -1 absent, 0 false, 1 true. */
    int use_timecode;
    int accurate_offsets;
} audd_enterprise_options_t;

/** Defaults: -1 in every numeric field (= "not set"). */
AUDD_API audd_enterprise_options_t audd_enterprise_options_default(void);

AUDD_API audd_error_t audd_recognize_enterprise(audd_client_t *client,
                                                const char *source,
                                                const audd_enterprise_options_t *options,
                                                audd_enterprise_result_t **out_result);

AUDD_API audd_error_t audd_recognize_enterprise_bytes(audd_client_t *client,
                                                      const void *data,
                                                      size_t size,
                                                      const audd_enterprise_options_t *options,
                                                      audd_enterprise_result_t **out_result);

AUDD_API void audd_enterprise_result_free(audd_enterprise_result_t *r);

/** Number of matches across all chunks. */
AUDD_API size_t audd_enterprise_result_count(const audd_enterprise_result_t *r);

/** Borrowed pointer to the i-th match. NULL if i out of range. */
AUDD_API const audd_enterprise_match_t *audd_enterprise_result_at(const audd_enterprise_result_t *r, size_t i);

/* Enterprise match getters. */
AUDD_API int         audd_enterprise_match_get_score(const audd_enterprise_match_t *m);
AUDD_API const char *audd_enterprise_match_get_timecode(const audd_enterprise_match_t *m);
AUDD_API const char *audd_enterprise_match_get_artist(const audd_enterprise_match_t *m);
AUDD_API const char *audd_enterprise_match_get_title(const audd_enterprise_match_t *m);
AUDD_API const char *audd_enterprise_match_get_album(const audd_enterprise_match_t *m);
AUDD_API const char *audd_enterprise_match_get_release_date(const audd_enterprise_match_t *m);
AUDD_API const char *audd_enterprise_match_get_label(const audd_enterprise_match_t *m);
AUDD_API const char *audd_enterprise_match_get_isrc(const audd_enterprise_match_t *m);
AUDD_API const char *audd_enterprise_match_get_upc(const audd_enterprise_match_t *m);
AUDD_API const char *audd_enterprise_match_get_song_link(const audd_enterprise_match_t *m);
AUDD_API int         audd_enterprise_match_get_start_offset(const audd_enterprise_match_t *m);
AUDD_API int         audd_enterprise_match_get_end_offset(const audd_enterprise_match_t *m);

/** Cover-art URL for lis.tn-hosted song_links; NULL otherwise. Borrowed. */
AUDD_API const char *audd_enterprise_match_thumbnail_url(const audd_enterprise_match_t *m);

/** lis.tn redirect URL for a streaming provider. NULL when not on lis.tn. */
AUDD_API const char *audd_enterprise_match_streaming_url(const audd_enterprise_match_t *m,
                                                          audd_provider_t provider);

/* ---------------------------------------------------------------------- *
 * Streams: management                                                      *
 * ---------------------------------------------------------------------- */

typedef struct {
    /** NULL-terminated array of provider strings. NULL = no return param. */
    const char **return_metadata;
} audd_streams_set_callback_url_options_t;

AUDD_API audd_error_t audd_streams_set_callback_url(audd_client_t *client,
                                                     const char *url,
                                                     const audd_streams_set_callback_url_options_t *options);

/**
 * Fetch the configured callback URL. Caller frees the returned string with
 * audd_string_free. Returns NULL into *out_url when none set on success.
 */
AUDD_API audd_error_t audd_streams_get_callback_url(audd_client_t *client,
                                                     char **out_url);

/** Free a heap string returned by audd_streams_get_callback_url and other
 *  audd_*_string getters. NULL-safe. */
AUDD_API void audd_string_free(char *s);

typedef struct {
    const char *url;
    int radio_id;
    /** NULL or "" → server default (callback at song end). "before" → at start. */
    const char *callbacks;
} audd_add_stream_request_t;

AUDD_API audd_error_t audd_streams_add(audd_client_t *client,
                                        const audd_add_stream_request_t *req);

AUDD_API audd_error_t audd_streams_set_url(audd_client_t *client,
                                            int radio_id,
                                            const char *url);

AUDD_API audd_error_t audd_streams_delete(audd_client_t *client,
                                           int radio_id);

AUDD_API audd_error_t audd_streams_list(audd_client_t *client,
                                         audd_stream_list_t **out_list);

AUDD_API void   audd_stream_list_free(audd_stream_list_t *list);
AUDD_API size_t audd_stream_list_count(const audd_stream_list_t *list);
AUDD_API const audd_stream_t *audd_stream_list_at(const audd_stream_list_t *list, size_t i);

AUDD_API int         audd_stream_get_radio_id(const audd_stream_t *s);
AUDD_API const char *audd_stream_get_url(const audd_stream_t *s);
AUDD_API int         audd_stream_get_running(const audd_stream_t *s);
AUDD_API const char *audd_stream_get_longpoll_category(const audd_stream_t *s);

/* ---------------------------------------------------------------------- *
 * Streams: callback parsing                                                *
 * ---------------------------------------------------------------------- */

/**
 * Parse a callback POST body into either a typed match or a typed
 * notification. On success, exactly one of out_match / out_notification
 * is set non-NULL; the other stays NULL.
 *
 * Caller frees the non-NULL output with the matching _free function.
 */
AUDD_API audd_error_t audd_parse_callback(const void *body,
                                          size_t size,
                                          audd_stream_callback_match_t **out_match,
                                          audd_stream_callback_notification_t **out_notification,
                                          char **out_error_message);

AUDD_API void audd_stream_callback_match_free(audd_stream_callback_match_t *m);
AUDD_API void audd_stream_callback_notification_free(audd_stream_callback_notification_t *n);

/* Stream callback match getters. */
AUDD_API int64_t     audd_stream_callback_match_get_radio_id(const audd_stream_callback_match_t *m);
AUDD_API const char *audd_stream_callback_match_get_timestamp(const audd_stream_callback_match_t *m);
AUDD_API int         audd_stream_callback_match_get_play_length(const audd_stream_callback_match_t *m);

/** Top match. Owned by `m`. Never NULL on a successfully parsed match. */
AUDD_API const audd_stream_callback_song_t *audd_stream_callback_match_get_song(const audd_stream_callback_match_t *m);

/** Number of alternative candidates. Almost always 0; nonzero only when
 *  the same fingerprint resolves to multiple variant catalog releases. */
AUDD_API size_t audd_stream_callback_match_alternatives_count(const audd_stream_callback_match_t *m);
AUDD_API const audd_stream_callback_song_t *audd_stream_callback_match_get_alternative(const audd_stream_callback_match_t *m, size_t i);

/** Full unparsed JSON for the callback body. Owned by `m`. */
AUDD_API const char *audd_stream_callback_match_raw_response(const audd_stream_callback_match_t *m);

/* Stream callback song getters. */
AUDD_API int         audd_stream_callback_song_get_score(const audd_stream_callback_song_t *s);
AUDD_API const char *audd_stream_callback_song_get_artist(const audd_stream_callback_song_t *s);
AUDD_API const char *audd_stream_callback_song_get_title(const audd_stream_callback_song_t *s);
AUDD_API const char *audd_stream_callback_song_get_album(const audd_stream_callback_song_t *s);
AUDD_API const char *audd_stream_callback_song_get_release_date(const audd_stream_callback_song_t *s);
AUDD_API const char *audd_stream_callback_song_get_label(const audd_stream_callback_song_t *s);
AUDD_API const char *audd_stream_callback_song_get_song_link(const audd_stream_callback_song_t *s);
AUDD_API const char *audd_stream_callback_song_get_isrc(const audd_stream_callback_song_t *s);
AUDD_API const char *audd_stream_callback_song_get_upc(const audd_stream_callback_song_t *s);

/* Stream callback notification getters. */
AUDD_API int         audd_stream_callback_notification_get_radio_id(const audd_stream_callback_notification_t *n);
/** -1 absent, 0 false, 1 true. */
AUDD_API int         audd_stream_callback_notification_get_stream_running(const audd_stream_callback_notification_t *n);
AUDD_API int         audd_stream_callback_notification_get_code(const audd_stream_callback_notification_t *n);
AUDD_API const char *audd_stream_callback_notification_get_message(const audd_stream_callback_notification_t *n);
AUDD_API int         audd_stream_callback_notification_get_time(const audd_stream_callback_notification_t *n);
AUDD_API const char *audd_stream_callback_notification_raw_response(const audd_stream_callback_notification_t *n);

/* ---------------------------------------------------------------------- *
 * Streams: longpoll category derivation (pure function)                    *
 * ---------------------------------------------------------------------- */

/**
 * Derive the 9-char longpoll category for a given API token + radio_id.
 *
 * Pure function — no network call. Useful for sharing categories with
 * browser/widget code without leaking the api_token (run on a server,
 * ship the 9-char category to the frontend).
 *
 * out_category must point to at least 10 bytes (9 hex + NUL).
 */
AUDD_API audd_error_t audd_derive_longpoll_category(const char *api_token,
                                                     int radio_id,
                                                     char *out_category);

/** Convenience wrapper: derives the category for `client`'s current token. */
AUDD_API audd_error_t audd_streams_derive_longpoll_category(const audd_client_t *client,
                                                             int radio_id,
                                                             char *out_category);

/* ---------------------------------------------------------------------- *
 * Streams: longpoll consumer (callback-driven)                             *
 * ---------------------------------------------------------------------- *
 *
 * C is synchronous — there's no idiomatic equivalent of Go's three typed
 * channels. Instead, a longpoll subscription is driven by registering
 * three callback functions:
 *
 *   - on_match(match, ud)        — fired once per recognition event
 *   - on_notification(notif, ud) — fired once per stream-lifecycle event
 *   - on_error(err, msg, ud)     — fired exactly once at termination
 *
 * audd_longpoll_run blocks the calling thread until either:
 *   - a terminal error fires (on_error then returns), OR
 *   - audd_longpoll_close is called from another thread.
 *
 * The handle written to *out_handle is the only argument
 * audd_longpoll_close accepts. It's safe to close from any thread; the
 * close is idempotent. Once audd_longpoll_run returns, the handle is no
 * longer valid — do not call audd_longpoll_close on it after.
 */

typedef void (*audd_on_match_fn)(const audd_stream_callback_match_t *match, void *ud);
typedef void (*audd_on_notification_fn)(const audd_stream_callback_notification_t *notif, void *ud);
typedef void (*audd_on_error_fn)(audd_error_t err, const char *message, void *ud);

typedef struct {
    audd_on_match_fn        on_match;
    audd_on_notification_fn on_notification;
    audd_on_error_fn        on_error;
    void                   *user_data;
} audd_longpoll_callbacks_t;

typedef struct {
    /** Unix timestamp to resume from. 0 = "start from now". */
    long since_time;
    /** Server-side longpoll wait in seconds. Default 50. */
    int  timeout;
    /** Set to 1 to skip the "callback URL configured?" preflight. */
    int  skip_callback_check;
} audd_longpoll_options_t;

AUDD_API audd_longpoll_options_t audd_longpoll_options_default(void);

/**
 * Block this thread polling `category` for stream events. Calls back the
 * given functions for each event. Returns when a terminal error fires or
 * when audd_longpoll_close is called from another thread.
 *
 * options may be NULL.
 *
 * If out_handle is non-NULL, the handle for audd_longpoll_close is written
 * to it BEFORE the first poll request — the caller can safely stash it for
 * an external close trigger. After audd_longpoll_run returns, the handle
 * is freed; do not access it.
 */
AUDD_API audd_error_t audd_longpoll_run(audd_client_t *client,
                                         const char *category,
                                         const audd_longpoll_options_t *options,
                                         const audd_longpoll_callbacks_t *callbacks,
                                         audd_longpoll_t **out_handle);

/** Trigger a graceful close on a running longpoll. Idempotent.
 *  May be called from any thread. */
AUDD_API void audd_longpoll_close(audd_longpoll_t *handle);

/* ---------------------------------------------------------------------- *
 * Custom catalog                                                           *
 * ---------------------------------------------------------------------- *
 *
 * NOT music recognition. Adds songs to your account's private fingerprint
 * database so AudD's recognition can later identify your own tracks for
 * your account only. Requires custom-catalog access on your token —
 * contact api@audd.io.
 */

AUDD_API audd_error_t audd_custom_catalog_add(audd_client_t *client,
                                                int audio_id,
                                                const char *source);

AUDD_API audd_error_t audd_custom_catalog_add_bytes(audd_client_t *client,
                                                     int audio_id,
                                                     const void *data,
                                                     size_t size);

/* ---------------------------------------------------------------------- *
 * Advanced (lyrics + raw escape hatch)                                     *
 * ---------------------------------------------------------------------- */

AUDD_API audd_error_t audd_advanced_find_lyrics(audd_client_t *client,
                                                 const char *query,
                                                 audd_lyrics_list_t **out_results);

AUDD_API void   audd_lyrics_list_free(audd_lyrics_list_t *list);
AUDD_API size_t audd_lyrics_list_count(const audd_lyrics_list_t *list);
AUDD_API const audd_lyrics_result_t *audd_lyrics_list_at(const audd_lyrics_list_t *list, size_t i);

AUDD_API const char *audd_lyrics_get_artist(const audd_lyrics_result_t *l);
AUDD_API const char *audd_lyrics_get_title(const audd_lyrics_result_t *l);
AUDD_API const char *audd_lyrics_get_lyrics(const audd_lyrics_result_t *l);
AUDD_API int         audd_lyrics_get_song_id(const audd_lyrics_result_t *l);
AUDD_API const char *audd_lyrics_get_media(const audd_lyrics_result_t *l);
AUDD_API const char *audd_lyrics_get_full_title(const audd_lyrics_result_t *l);
AUDD_API int         audd_lyrics_get_artist_id(const audd_lyrics_result_t *l);
AUDD_API const char *audd_lyrics_get_song_link(const audd_lyrics_result_t *l);

/**
 * Escape hatch: hits POST https://api.audd.io/<method>/ with the given
 * form params and returns the raw JSON response body as a heap string.
 * Caller frees with audd_string_free.
 *
 * params is a NULL-terminated array of {key, value} pairs:
 *   const char *params[] = { "q", "song name", "limit", "1", NULL };
 */
AUDD_API audd_error_t audd_advanced_raw_request(audd_client_t *client,
                                                 const char *method,
                                                 const char **params_kv,
                                                 char **out_response);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AUDD_H */
