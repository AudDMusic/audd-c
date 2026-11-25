/* test_fixtures.h — read the JSON fixtures under tests/fixtures/ (copies of
 * the audd-openapi fixtures). AUDD_TEST_FIXTURES_DIR is set by CMake. */
#ifndef AUDD_TEST_FIXTURES_H
#define AUDD_TEST_FIXTURES_H

#include <stdio.h>
#include <stdlib.h>

#ifndef AUDD_TEST_FIXTURES_DIR
#  error "AUDD_TEST_FIXTURES_DIR must be defined"
#endif

/* Heap copy (libc malloc, NUL-terminated) of fixture `name`; NULL on failure.
 * Free with free(). */
static char *read_fixture(const char *name, size_t *len_out)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", AUDD_TEST_FIXTURES_DIR, name);
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    char *buf = NULL;
    size_t len = 0, cap = 0;
    for (;;) {
        if (len + 4096 + 1 > cap) {
            size_t ncap = cap == 0 ? 8192 : cap * 2;
            char *nb = (char *)realloc(buf, ncap);
            if (nb == NULL) { free(buf); fclose(f); return NULL; }
            buf = nb;
            cap = ncap;
        }
        size_t n = fread(buf + len, 1, 4096, f);
        len += n;
        if (n < 4096) break;
    }
    fclose(f);
    buf[len] = '\0';
    if (len_out) *len_out = len;
    return buf;
}

#endif /* AUDD_TEST_FIXTURES_H */
