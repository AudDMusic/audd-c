/* recognize_file.c — identify a song from a local file path.
 *
 * Run:
 *   ./recognize_file /path/to/clip.mp3
 */
#include <audd.h>

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file.mp3>\n", argv[0]);
        return 2;
    }
    /* Token from AUDD_API_TOKEN if set, else built-in "test" demo. */
    audd_client_t *client = audd_client_new(NULL, NULL);
    if (audd_client_api_token(client) == NULL) {
        audd_client_set_api_token(client, "test");
    }
    audd_recognition_t *r = NULL;
    audd_error_t e = audd_recognize(client, argv[1], NULL, &r);
    if (e != AUDD_OK) {
        fprintf(stderr, "%s\n", audd_last_error_message(client));
        audd_client_free(client);
        return 1;
    }
    if (r == NULL) {
        printf("no match\n");
    } else {
        printf("matched: %s — %s\n",
               audd_recognition_get_artist(r),
               audd_recognition_get_title(r));
        if (audd_recognition_get_album(r))
            printf("album:    %s\n", audd_recognition_get_album(r));
        if (audd_recognition_get_release_date(r))
            printf("released: %s\n", audd_recognition_get_release_date(r));
        if (audd_recognition_get_label(r))
            printf("label:    %s\n", audd_recognition_get_label(r));
        if (audd_recognition_get_song_link(r))
            printf("link:     %s\n", audd_recognition_get_song_link(r));
        audd_recognition_free(r);
    }
    audd_client_free(client);
    return 0;
}
