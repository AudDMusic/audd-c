/* streams_setup.c — register a callback URL and add streams to monitor.
 *
 * Run:
 *   ./streams_setup
 */
#include <audd.h>

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    audd_client_t *client = audd_client_new(NULL, NULL);
    if (client == NULL) return 1;

    /* 1. Tell AudD where to POST recognition results for our account. */
    const char *return_md[] = { "apple_music", "spotify", NULL };
    audd_streams_set_callback_url_options_t set_opts = { return_md };
    audd_error_t e = audd_streams_set_callback_url(client,
        "https://your.app/audd/callback", &set_opts);
    if (e != AUDD_OK) {
        fprintf(stderr, "set_callback_url: %s\n", audd_last_error_message(client));
        audd_client_free(client);
        return 1;
    }

    /* 2. Add some streams. */
    audd_add_stream_request_t r1 = { "https://example.com/radio.m3u8", 1, NULL };
    audd_add_stream_request_t r2 = { "twitch:somechannel", 2, NULL };
    if (audd_streams_add(client, &r1) != AUDD_OK) {
        fprintf(stderr, "add stream 1: %s\n", audd_last_error_message(client));
    }
    if (audd_streams_add(client, &r2) != AUDD_OK) {
        fprintf(stderr, "add stream 2: %s\n", audd_last_error_message(client));
    }

    /* 3. List configured streams. */
    audd_stream_list_t *list = NULL;
    if (audd_streams_list(client, &list) == AUDD_OK) {
        printf("%zu stream(s) configured:\n", audd_stream_list_count(list));
        for (size_t i = 0; i < audd_stream_list_count(list); ++i) {
            const audd_stream_t *s = audd_stream_list_at(list, i);
            printf("  radio_id=%d  url=%s  running=%d\n",
                   audd_stream_get_radio_id(s),
                   audd_stream_get_url(s),
                   audd_stream_get_running(s));
        }
        audd_stream_list_free(list);
    }
    audd_client_free(client);
    return 0;
}
