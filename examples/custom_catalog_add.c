/* custom_catalog_add.c — fingerprint a track and store it in the private
 * catalog so AudD's recognition can later identify it for *your account*.
 *
 * NOT a recognition call. For recognition use recognize_url / recognize_file.
 *
 * Run:
 *   ./custom_catalog_add <audio_id> <file>
 */
#include <audd.h>

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <audio_id> <file>\n", argv[0]);
        return 2;
    }
    int audio_id = atoi(argv[1]);
    const char *path = argv[2];

    audd_client_t *client = audd_client_new(NULL, NULL);
    if (client == NULL) return 1;

    audd_error_t e = audd_custom_catalog_add(client, audio_id, path);
    if (e != AUDD_OK) {
        fprintf(stderr, "%s\n", audd_last_error_message(client));
        audd_client_free(client);
        return 1;
    }
    printf("ok: %s stored under audio_id=%d\n", path, audio_id);
    audd_client_free(client);
    return 0;
}
