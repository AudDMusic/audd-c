/* streams_callback_handler.c — parse a stream callback POST body.
 *
 * The SDK is transport-agnostic: feed the raw POST body bytes into
 * audd_parse_callback() from any HTTP framework you embed (libmicrohttpd,
 * mongoose, civetweb, etc). This example reads bytes from stdin so you
 * can pipe a captured webhook payload in:
 *
 *   curl -d @captured.json ./streams_callback_handler  # demo loop
 *   cat captured.json | ./streams_callback_handler
 */
#include <audd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_stdin(char **out, size_t *out_len)
{
    size_t cap = 4096, len = 0;
    char *buf = (char *)malloc(cap);
    if (buf == NULL) return -1;
    int c;
    while ((c = getchar()) != EOF) {
        if (len + 1 >= cap) {
            cap *= 2;
            char *p = (char *)realloc(buf, cap);
            if (p == NULL) { free(buf); return -1; }
            buf = p;
        }
        buf[len++] = (char)c;
    }
    buf[len] = '\0';
    *out = buf;
    *out_len = len;
    return 0;
}

int main(void)
{
    char *body = NULL;
    size_t body_len = 0;
    if (read_stdin(&body, &body_len) != 0 || body_len == 0) {
        fprintf(stderr, "usage: pipe a callback POST body to stdin\n");
        return 2;
    }

    audd_stream_callback_match_t *m = NULL;
    audd_stream_callback_notification_t *n = NULL;
    char *err = NULL;
    audd_error_t e = audd_parse_callback(body, body_len, &m, &n, &err);
    if (e != AUDD_OK) {
        fprintf(stderr, "parse error: %s\n", err ? err : audd_error_string(e));
        audd_string_free(err);
        free(body);
        return 1;
    }
    if (m != NULL) {
        const audd_stream_callback_song_t *song = audd_stream_callback_match_get_song(m);
        printf("matched on radio_id=%lld: %s — %s\n",
               (long long)audd_stream_callback_match_get_radio_id(m),
               audd_stream_callback_song_get_artist(song),
               audd_stream_callback_song_get_title(song));
        size_t alts = audd_stream_callback_match_alternatives_count(m);
        for (size_t i = 0; i < alts; ++i) {
            const audd_stream_callback_song_t *a =
                audd_stream_callback_match_get_alternative(m, i);
            printf("  alt: %s — %s\n",
                   audd_stream_callback_song_get_artist(a),
                   audd_stream_callback_song_get_title(a));
        }
        audd_stream_callback_match_free(m);
    } else if (n != NULL) {
        printf("notification: radio_id=%d code=%d msg=%s\n",
               audd_stream_callback_notification_get_radio_id(n),
               audd_stream_callback_notification_get_code(n),
               audd_stream_callback_notification_get_message(n));
        audd_stream_callback_notification_free(n);
    }
    audd_string_free(err);
    free(body);
    return 0;
}
