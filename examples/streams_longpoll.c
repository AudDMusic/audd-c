/* streams_longpoll.c — receive stream events via longpoll (no callback URL
 * receiver needed).
 *
 * Run:
 *   ./streams_longpoll <radio_id>
 *
 * Press Ctrl-C to stop.
 */
#include <audd.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static audd_longpoll_t *g_handle;

static void on_match(const audd_stream_callback_match_t *m, void *ud)
{
    (void)ud;
    const audd_stream_callback_song_t *s = audd_stream_callback_match_get_song(m);
    printf("matched: %s — %s\n",
           audd_stream_callback_song_get_artist(s),
           audd_stream_callback_song_get_title(s));
}

static void on_notification(const audd_stream_callback_notification_t *n, void *ud)
{
    (void)ud;
    printf("notification: %s\n",
           audd_stream_callback_notification_get_message(n));
}

static void on_error(audd_error_t err, const char *msg, void *ud)
{
    (void)ud;
    fprintf(stderr, "error: %s — %s\n", audd_error_string(err), msg);
}

static void on_sigint(int sig)
{
    (void)sig;
    audd_longpoll_close(g_handle); /* idempotent, signal-safe enough for demo */
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <radio_id>\n", argv[0]);
        return 2;
    }
    int radio_id = atoi(argv[1]);

    audd_client_t *client = audd_client_new(NULL, NULL);
    if (client == NULL) return 1;

    char category[10];
    if (audd_streams_derive_longpoll_category(client, radio_id, category) != AUDD_OK) {
        fprintf(stderr, "%s\n", audd_last_error_message(client));
        audd_client_free(client);
        return 1;
    }

    audd_longpoll_callbacks_t cb = {
        .on_match = on_match,
        .on_notification = on_notification,
        .on_error = on_error,
        .user_data = NULL,
    };
    signal(SIGINT, on_sigint);

    audd_error_t e = audd_longpoll_run(client, category, NULL, &cb, &g_handle);
    if (e != AUDD_OK) {
        fprintf(stderr, "longpoll terminated: %s\n", audd_error_string(e));
    }
    audd_client_free(client);
    return e == AUDD_OK ? 0 : 1;
}
