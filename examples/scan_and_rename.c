/* scan_and_rename.c — recognize each .mp3 in a directory, print
 * "Artist - Title" alongside the original filename. Doesn't actually
 * rename — adapt for your needs.
 *
 * Run:
 *   ./scan_and_rename /path/to/dir
 */
#include <audd.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int has_audio_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (dot == NULL) return 0;
    static const char *ext[] = { ".mp3", ".m4a", ".flac", ".wav", ".ogg", NULL };
    for (int i = 0; ext[i]; ++i) {
        if (strcasecmp(dot, ext[i]) == 0) return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s <dir>\n", argv[0]); return 2; }
    DIR *d = opendir(argv[1]);
    if (d == NULL) { perror("opendir"); return 1; }

    audd_client_t *client = audd_client_new(NULL, NULL);
    if (client == NULL) { closedir(d); return 1; }

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (!has_audio_ext(de->d_name)) continue;
        char path[2048];
        snprintf(path, sizeof(path), "%s/%s", argv[1], de->d_name);
        audd_recognition_t *r = NULL;
        audd_error_t e = audd_recognize(client, path, NULL, &r);
        if (e != AUDD_OK) {
            fprintf(stderr, "%s: error: %s\n", de->d_name, audd_last_error_message(client));
            continue;
        }
        if (r == NULL) {
            printf("%-40s  no match\n", de->d_name);
        } else {
            printf("%-40s  %s — %s\n", de->d_name,
                   audd_recognition_get_artist(r),
                   audd_recognition_get_title(r));
            audd_recognition_free(r);
        }
    }
    closedir(d);
    audd_client_free(client);
    return 0;
}
