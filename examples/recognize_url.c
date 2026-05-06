/* recognize_url.c — identify a song hosted at a URL.
 *
 * Run:
 *   ./recognize_url https://audd.tech/example.mp3
 */
#include <audd.h>

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    const char *url = argc > 1 ? argv[1] : "https://audd.tech/example.mp3";
    /* Get a real token at https://dashboard.audd.io */
    audd_client_t *client = audd_client_new("test", NULL);
    if (client == NULL) { fprintf(stderr, "out of memory\n"); return 1; }

    audd_recognition_t *r = NULL;
    audd_error_t e = audd_recognize(client, url, NULL, &r);
    if (e != AUDD_OK) {
        fprintf(stderr, "audd error: %s — %s\n",
                audd_error_string(e), audd_last_error_message(client));
        audd_client_free(client);
        return 1;
    }
    if (r == NULL) {
        printf("no match\n");
    } else {
        printf("%s — %s\n",
               audd_recognition_get_artist(r),
               audd_recognition_get_title(r));
        const char *thumb = audd_recognition_thumbnail_url(r);
        if (thumb) printf("cover:    %s\n", thumb);
        const char *spo = audd_recognition_streaming_url(r, AUDD_PROVIDER_SPOTIFY);
        if (spo)   printf("spotify:  %s\n", spo);
        audd_recognition_free(r);
    }
    audd_client_free(client);
    return 0;
}
