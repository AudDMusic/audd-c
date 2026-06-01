# audd-c

[![CI](https://github.com/AudDMusic/audd-c/actions/workflows/ci.yml/badge.svg)](https://github.com/AudDMusic/audd-c/actions/workflows/ci.yml)

Official C SDK for [music recognition API](https://audd.io): identify music from a short audio clip, a long audio file, or a live stream.

The API itself is so simple that it can easily be used even without an SDK: [docs.audd.io](https://docs.audd.io).

## Hello, AudD

Build with CMake (3.16+) and link against `audd`. Requires libcurl
development headers on your system; everything else (cJSON for JSON,
optional Unity for tests) is vendored under `vendor/`.

```sh
# Debian / Ubuntu
sudo apt-get install libcurl4-openssl-dev cmake

# macOS
brew install curl cmake
```

Drop the SDK into your CMake project via `FetchContent`:

```cmake
include(FetchContent)
FetchContent_Declare(audd
    GIT_REPOSITORY https://github.com/AudDMusic/audd-c.git
    GIT_TAG        v1.5.7
)
FetchContent_MakeAvailable(audd)

target_link_libraries(your_app PRIVATE audd)
```

Or build & install system-wide:

```sh
git clone https://github.com/AudDMusic/audd-c
cmake -S audd-c -B audd-c/build -DCMAKE_BUILD_TYPE=Release
cmake --build audd-c/build -j
sudo cmake --install audd-c/build
```

Get your API token at [dashboard.audd.io](https://dashboard.audd.io).

Identify a song hosted at a URL:

```c
#include <audd.h>
#include <stdio.h>

int main(void) {
    audd_client_t *client = audd_client_new("your-api-token", NULL);

    audd_recognition_t *r = NULL;
    audd_error_t e = audd_recognize(client,
        "https://audd.tech/example.mp3", NULL, &r);

    if (e != AUDD_OK) {
        fprintf(stderr, "%s\n", audd_last_error_message(client));
    } else if (r == NULL) {
        printf("no match\n");
    } else {
        printf("%s — %s\n",
               audd_recognition_get_artist(r),
               audd_recognition_get_title(r));
        audd_recognition_free(r);
    }
    audd_client_free(client);
    return 0;
}
```

Identify a song from a local file path — same call, the SDK auto-detects
URLs vs. paths:

```c
audd_recognize(client, "/path/to/clip.mp3", NULL, &r);
```

For longer audio files, use `audd_recognize_enterprise`,
which returns multiple matches across the file's chunks. Each match
carries the same core tags plus `score`, `start_seconds`, `end_seconds`,
`start_offset`, `end_offset`, `isrc`, `upc`. Access to `isrc`, `upc`, and
`score` requires a Startup plan or higher — [contact us](mailto:api@audd.io)
for enterprise features.

`start_seconds` and `end_seconds` tell you where the match plays in your
file, in seconds; they are `-1.0` when unknown. They are precise because the
SDK requests accurate offsets by default (set `opts.accurate_offsets = 0` to
opt out). `start_offset` and `end_offset` are the raw fragment-relative
milliseconds behind them.

```c
audd_enterprise_options_t opts = audd_enterprise_options_default();
opts.limit = 1;

audd_enterprise_result_t *r = NULL;
audd_recognize_enterprise(client, "/long/file.mp3", &opts, &r);

for (size_t i = 0; i < audd_enterprise_result_count(r); ++i) {
    const audd_enterprise_match_t *m = audd_enterprise_result_at(r, i);
    printf("[%d] %s — %s  (%.1fs-%.1fs)\n",
           audd_enterprise_match_get_score(m),
           audd_enterprise_match_get_artist(m),
           audd_enterprise_match_get_title(m),
           audd_enterprise_match_get_start_seconds(m),
           audd_enterprise_match_get_end_seconds(m));
}
audd_enterprise_result_free(r);
```

Requires a C99 compiler.

## Authentication

Pass your token to `audd_client_new`:

```c
audd_client_t *client = audd_client_new("your-api-token", NULL);
```

Or pass `NULL` and the SDK reads the `AUDD_API_TOKEN` environment variable:

```c
/* AUDD_API_TOKEN=your-token … */
audd_client_t *client = audd_client_new(NULL, NULL);
```

Get a real token at [dashboard.audd.io](https://dashboard.audd.io). The
public `"test"` token works for the snippets above but is capped at 10
requests.

For long-running services that pull tokens from a secret manager and
need to swap them without restarting:

```c
audd_client_set_api_token(client, new_token); /* atomic */
```

In-flight requests continue with the previous token; subsequent ones use
the new value.

If you'd rather fail fast at construction time when no token is
configured, use `audd_client_new_strict`, which writes
`AUDD_ERR_AUTHENTICATION` to its `audd_error_t *` out-param and returns
`NULL`.

## What you get back

By default `audd_recognize` returns the core tags plus AudD's universal
song link — no metadata-block opt-in needed:

```c
audd_recognition_t *r = NULL;
audd_recognize(client, "https://audd.tech/example.mp3", NULL, &r);

printf("%s — %s\n",
       audd_recognition_get_artist(r),
       audd_recognition_get_title(r));
printf("Album:    %s\n", audd_recognition_get_album(r));
printf("Released: %s\n", audd_recognition_get_release_date(r));
printf("Label:    %s\n", audd_recognition_get_label(r));
printf("AudD:     %s\n", audd_recognition_get_song_link(r));

/* Helpers, driven off song_link — work without any return-metadata opt-in: */
printf("Cover:    %s\n", audd_recognition_thumbnail_url(r));
printf("Spotify:  %s\n", audd_recognition_streaming_url(r, AUDD_PROVIDER_SPOTIFY));

audd_recognition_free(r);
```

If you need provider-specific metadata blocks, opt in per call. Request
only what you need — each provider you ask for adds latency:

```c
const char *want[] = { "apple_music", "spotify", NULL };
audd_recognize_options_t opts = { .return_metadata = want };

audd_recognition_t *r = NULL;
audd_recognize(client, "https://audd.tech/example.mp3", &opts, &r);

const audd_apple_music_t *am = audd_recognition_apple_music(r);
const audd_spotify_t     *sp = audd_recognition_spotify(r);
if (am) printf("Apple Music: %s\n", audd_apple_music_get_url(am));
if (sp) printf("Spotify URI: %s\n", audd_spotify_get_uri(sp));
printf("Preview:     %s\n", audd_recognition_preview_url(r));
```

Valid `return_metadata` values: `apple_music`, `spotify`, `deezer`,
`napster`, `musicbrainz`. Each metadata-block accessor returns `NULL`
when the corresponding block wasn't requested or wasn't returned.

### Reading additional metadata

Every result, every metadata block, and every match has typed accessors
plus a generic `*_extra(obj, "key")` escape hatch that returns the field's
unparsed JSON. Use it to read fields outside the typed surface
as typed properties:

```c
const char *song_length_json = audd_recognition_extra(r, "song_length");
/* song_length_json is e.g. "120" — parse with whatever JSON tool you like. */

const audd_apple_music_t *am = audd_recognition_apple_music(r);
const char *genres_json = audd_apple_music_extra(am, "genreNames");
/* "[\"Pop\",\"Rock\"]" — caller-driven parsing keeps the SDK lean. */
```

`audd_recognition_raw_response(r)` returns the full unparsed JSON of the
result block when you'd rather walk it yourself.

For sending arbitrary form fields the typed parameters don't cover, pass an
`extra_parameters` NULL-terminated array (alternating keys and values) via the
options struct:

```c
const char *extras[] = { "my_custom_flag", "1", NULL };
audd_recognize_options_t opts = {0};
opts.return_metadata = (const char *[]){ "apple_music", NULL };
opts.extra_parameters = extras;
audd_recognize(client, source, &opts, &r);
```

Typed parameters win on collision.

## Errors

Every fallible call returns an `audd_error_t` enum. Match by category:

```c
audd_error_t e = audd_recognize(client, source, NULL, &r);
switch (e) {
case AUDD_OK: break;
case AUDD_ERR_AUTHENTICATION: /* 900 / 901 / 903 — token problems */ break;
case AUDD_ERR_QUOTA:          /* 902 — quota exceeded */            break;
case AUDD_ERR_INVALID_AUDIO:  /* 300 / 400 / 500 — audio is bad */   break;
case AUDD_ERR_RATE_LIMIT:     /* 611 — back off and retry */         break;
case AUDD_ERR_SERVER:         /* 5xx, non-JSON gateway */            break;
default:
    fprintf(stderr, "audd: %s — %s\n",
            audd_error_string(e), audd_last_error_message(client));
}
```

For the underlying numeric AudD code (e.g. 901, 904) and the X-Request-Id
of the response, use the per-client accessors:

```c
int code = audd_last_error_code(client);
const char *msg = audd_last_error_message(client);
const char *rid = audd_last_request_id(client);
```

Sentinels: `AUDD_ERR_AUTHENTICATION`, `AUDD_ERR_QUOTA`,
`AUDD_ERR_SUBSCRIPTION`, `AUDD_ERR_CUSTOM_CATALOG_ACCESS`,
`AUDD_ERR_INVALID_REQUEST`, `AUDD_ERR_INVALID_AUDIO`,
`AUDD_ERR_RATE_LIMIT`, `AUDD_ERR_STREAM_LIMIT`,
`AUDD_ERR_NOT_RELEASED`, `AUDD_ERR_BLOCKED`, `AUDD_ERR_NEEDS_UPDATE`,
`AUDD_ERR_SERVER`, `AUDD_ERR_CONNECTION`, `AUDD_ERR_SERIALIZATION`,
`AUDD_ERR_INVALID_ARGUMENT`, `AUDD_ERR_OUT_OF_MEMORY`.

## Configuration

```c
audd_options_t opts = audd_options_default();
opts.standard_timeout_seconds   = 60;
opts.enterprise_timeout_seconds = 7200;
opts.max_attempts               = 5;
opts.backoff_ms                 = 1000;
opts.user_agent_suffix          = "myapp/1.0";
opts.ca_bundle_path             = "/etc/ssl/certs/ca-bundle.crt";

audd_client_t *client = audd_client_new("your-api-token", &opts);
```

Retries are cost-aware:

- Read endpoints retry on 408/429/5xx and any net error.
- Recognition endpoints retry only on 5xx and pre-upload connection
  failures (DNS, TCP dial). Post-upload errors are not retried — the
  server may have already done the metered work.
- Mutating endpoints retry only on pre-upload connection failures.

### Plugging in a custom allocator

Embedded targets, profilers, or sandboxes can replace the SDK's allocator
once at process start, before any other `audd_*` call:

```c
audd_allocator_t alloc = {
    .malloc_fn  = my_arena_malloc,
    .free_fn    = my_arena_free,
    .realloc_fn = my_arena_realloc,
};
audd_set_allocator(&alloc);
```

Pass `NULL` to restore the libc defaults. The same hooks are wired
through to the bundled cJSON parser — no allocations leak out.

## Streams

Stream recognition turns AudD into a continuous monitor for an audio
stream (internet radio, Twitch, YouTube live, raw HLS/Icecast) and
notifies you for every recognized song. Set up streams once, then either
receive matches via a callback URL or longpoll for them.

```c
/* 1. Tell AudD where to POST recognition results for your account. */
const char *want[] = { "apple_music", "spotify", NULL };
audd_streams_set_callback_url_options_t opts = { want };
audd_streams_set_callback_url(client, "https://your.app/audd/callback", &opts);

/* 2. Add streams to monitor. */
audd_add_stream_request_t r1 = { "https://example.com/radio.m3u8", 1, NULL };
audd_add_stream_request_t r2 = { "twitch:somechannel", 2, NULL };
audd_streams_add(client, &r1);
audd_streams_add(client, &r2);

/* 3. Inspect what you have configured. */
audd_stream_list_t *list = NULL;
audd_streams_list(client, &list);
for (size_t i = 0; i < audd_stream_list_count(list); ++i) {
    const audd_stream_t *s = audd_stream_list_at(list, i);
    printf("%d %s running=%d\n",
           audd_stream_get_radio_id(s),
           audd_stream_get_url(s),
           audd_stream_get_running(s));
}
audd_stream_list_free(list);
```

Inside your callback receiver — whatever HTTP framework you've embedded
(libmicrohttpd, mongoose, civetweb, your own server) — read the POST
body bytes and parse them with `audd_parse_callback`:

```c
audd_stream_callback_match_t *match = NULL;
audd_stream_callback_notification_t *notif = NULL;
char *err = NULL;
audd_error_t e = audd_parse_callback(body, body_len, &match, &notif, &err);
if (e != AUDD_OK) {
    /* Respond 400 with err to the framework. */
    audd_string_free(err);
    return;
}
if (match != NULL) {
    const audd_stream_callback_song_t *s = audd_stream_callback_match_get_song(match);
    printf("matched: %s — %s\n",
           audd_stream_callback_song_get_artist(s),
           audd_stream_callback_song_get_title(s));
    audd_stream_callback_match_free(match);
}
if (notif != NULL) {
    printf("notification: %s\n",
           audd_stream_callback_notification_get_message(notif));
    audd_stream_callback_notification_free(notif);
}
```

See [`examples/streams_callback_handler.c`](examples/streams_callback_handler.c)
and [`examples/streams_setup.c`](examples/streams_setup.c) for runnable code.

### Receiving events without a callback URL (longpoll)

If hosting a callback receiver isn't an option, longpoll for events
from the client side. C is synchronous, so the consumer is built around
three callbacks plus a blocking call:

```c
int radio_id = 1; /* any integer you choose — your handle for this stream */

audd_longpoll_callbacks_t cb = {
    .on_match        = on_match,
    .on_notification = on_notification,
    .on_error        = on_error,
    .user_data       = my_state,
};
audd_longpoll_t *handle = NULL;
audd_longpoll_run_by_radio_id(client, radio_id, NULL, &cb, &handle);
/* blocks until on_error fires or audd_longpoll_close(handle) is called
   from another thread. */
```

`audd_derive_longpoll_category(token, radio_id, out)` is also available
as a free function for computing categories on a server and shipping them
to a frontend that consumes the longpoll stream — no api_token leaves
the server.

## Custom catalog (advanced)

> [!WARNING]
> The custom-catalog endpoint is **not** music recognition. It adds songs
> to your account's **private fingerprint database**, so AudD's
> recognition can later identify *your own* tracks for *your account
> only*. If you intended to identify music, use `audd_recognize` (or
> `audd_recognize_enterprise` for longer audio files) instead.

```c
audd_custom_catalog_add(client, /*audio_id=*/42, "/path/to/track.mp3");
```

Custom-catalog access requires a separate subscription. Contact
api@audd.io to get it enabled.

## Advanced

`audd_advanced_find_lyrics` exposes the typed lyrics endpoint, and
`audd_advanced_raw_request` is a generic escape hatch for AudD endpoints
not yet wrapped by this SDK:

```c
const char *params[] = { "param", "value", NULL };
char *response_json = NULL;
audd_advanced_raw_request(client, "newMethodName", params, &response_json);
/* response_json is the raw JSON body. Parse with whatever you prefer. */
audd_string_free(response_json);
```

## Memory model

- Every `audd_*_new` / `audd_*_recognize` / `audd_*_list` function
  documents whether the caller owns the returned handle and which
  `_free` function releases it.
- Strings returned by `_get_*` accessors are owned by the parent handle.
  Copy them with `strdup` if you want them to outlive the handle.
- `audd_string_free` releases heap strings returned by accessors that
  explicitly hand off ownership (`audd_streams_get_callback_url`,
  `audd_advanced_raw_request`). It's NULL-safe.

## License & support

MIT — see [LICENSE](./LICENSE). Security policy: [SECURITY.md](./SECURITY.md).

Vendored: [cJSON](https://github.com/DaveGamble/cJSON) (MIT) for JSON
parsing; [Unity](https://github.com/ThrowTheSwitch/Unity) (MIT) for tests.

Bug reports and PRs welcome. For account / API questions, email
api@audd.io.
