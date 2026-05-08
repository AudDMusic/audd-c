/* recognize_enterprise.c — identify songs in a long file via the
 * enterprise endpoint. Returns one match per recognized chunk.
 *
 * Run:
 *   ./recognize_enterprise <file>
 */
#include <audd.h>

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s <audio_file>\n", argv[0]); return 2; }

    audd_client_t *client = audd_client_new(NULL, NULL);
    if (client == NULL) return 1;

    audd_enterprise_options_t opts = audd_enterprise_options_default();
    opts.limit = 1; /* one match per chunk; safe default while developing */

    audd_enterprise_result_t *r = NULL;
    audd_error_t e = audd_recognize_enterprise(client, argv[1], &opts, &r);
    if (e != AUDD_OK) {
        fprintf(stderr, "%s\n", audd_last_error_message(client));
        audd_client_free(client);
        return 1;
    }
    size_t n = audd_enterprise_result_count(r);
    printf("%zu match(es):\n", n);
    for (size_t i = 0; i < n; ++i) {
        const audd_enterprise_match_t *m = audd_enterprise_result_at(r, i);
        printf("  [%d] %s — %s  (start=%d end=%d)\n",
               audd_enterprise_match_get_score(m),
               audd_enterprise_match_get_artist(m),
               audd_enterprise_match_get_title(m),
               audd_enterprise_match_get_start_offset(m),
               audd_enterprise_match_get_end_offset(m));
        const char *isrc = audd_enterprise_match_get_isrc(m);
        if (isrc && *isrc) printf("       ISRC: %s\n", isrc);
    }
    audd_enterprise_result_free(r);
    audd_client_free(client);
    return 0;
}
