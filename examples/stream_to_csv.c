/* stream_to_csv.c — long-poll a stream and append each match to a CSV file.
 *
 * Run:
 *   ./stream_to_csv <radio_id> <output.csv>
 */
#include <audd.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FILE *out;
} ctx_t;

static audd_longpoll_t *g_handle;

static void csv_quote(FILE *f, const char *s)
{
    fputc('"', f);
    if (s) {
        for (const char *p = s; *p; ++p) {
            if (*p == '"') { fputc('"', f); fputc('"', f); }
            else fputc(*p, f);
        }
    }
    fputc('"', f);
}

static void on_match(const audd_stream_callback_match_t *m, void *ud)
{
    ctx_t *c = (ctx_t *)ud;
    const audd_stream_callback_song_t *song = audd_stream_callback_match_get_song(m);
    csv_quote(c->out, audd_stream_callback_match_get_timestamp(m)); fputc(',', c->out);
    fprintf(c->out, "%lld,", (long long)audd_stream_callback_match_get_radio_id(m));
    csv_quote(c->out, audd_stream_callback_song_get_artist(song)); fputc(',', c->out);
    csv_quote(c->out, audd_stream_callback_song_get_title(song));  fputc('\n', c->out);
    fflush(c->out);
}

static void on_error(audd_error_t err, const char *msg, void *ud)
{
    (void)ud;
    fprintf(stderr, "error: %s — %s\n", audd_error_string(err), msg);
}

static void on_sigint(int sig) { (void)sig; audd_longpoll_close(g_handle); }

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <radio_id> <output.csv>\n", argv[0]);
        return 2;
    }
    int radio_id = atoi(argv[1]);
    FILE *out = fopen(argv[2], "a");
    if (out == NULL) { perror("fopen"); return 1; }
    if (ftell(out) == 0) {
        fprintf(out, "timestamp,radio_id,artist,title\n");
    }
    audd_client_t *client = audd_client_new(NULL, NULL);
    if (client == NULL) { fclose(out); return 1; }

    char category[10];
    if (audd_streams_derive_longpoll_category(client, radio_id, category) != AUDD_OK) {
        fprintf(stderr, "%s\n", audd_last_error_message(client));
        audd_client_free(client); fclose(out); return 1;
    }

    ctx_t ctx = { out };
    audd_longpoll_callbacks_t cb = { on_match, NULL, on_error, &ctx };
    signal(SIGINT, on_sigint);
    audd_error_t e = audd_longpoll_run(client, category, NULL, &cb, &g_handle);
    audd_client_free(client);
    fclose(out);
    return e == AUDD_OK ? 0 : 1;
}
