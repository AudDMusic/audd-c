/* audd_url.c — minimal URL helpers (no full URL parser; just what we need). */
#include "audd_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int audd_url_is_http(const char *url)
{
    if (url == NULL) return 0;
    return (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0);
}

/* Parse out [scheme:]//host[:port]/path?query#frag.
 * Returns 1 if hostname matches `host`. Case-insensitive on host. */
int audd_url_hostname_is(const char *url, const char *host)
{
    if (url == NULL || host == NULL) return 0;
    const char *p = strstr(url, "://");
    if (p == NULL) return 0;
    p += 3;
    /* Skip user-info if present: name[:pass]@host */
    const char *at = strchr(p, '@');
    const char *path = strpbrk(p, "/?#");
    if (at != NULL && (path == NULL || at < path)) {
        p = at + 1;
    }
    /* Hostname ends at one of [ : / ? # ] or end-of-string. */
    const char *end = p;
    while (*end != '\0' && *end != ':' && *end != '/' && *end != '?' && *end != '#') {
        ++end;
    }
    size_t hlen = (size_t)(end - p);
    if (hlen != strlen(host)) return 0;
    for (size_t i = 0; i < hlen; ++i) {
        char a = (char)tolower((unsigned char)p[i]);
        char b = (char)tolower((unsigned char)host[i]);
        if (a != b) return 0;
    }
    return 1;
}

/* URL-encode `s` into a heap string. Unreserved = ALPHA/DIGIT/-/./_/~ */
static char *url_encode(const char *s)
{
    if (s == NULL) return audd_strdup("");
    size_t in_len = strlen(s);
    /* Worst case: every char becomes %XX. */
    char *out = (char *)audd_malloc(in_len * 3 + 1);
    if (out == NULL) return NULL;
    size_t o = 0;
    for (size_t i = 0; i < in_len; ++i) {
        unsigned char c = (unsigned char)s[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '.' || c == '_' || c == '~') {
            out[o++] = (char)c;
        } else {
            static const char hex[] = "0123456789ABCDEF";
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 0xf];
        }
    }
    out[o] = '\0';
    return out;
}

/* Append "?return=<csv>" or "&return=<csv>" to raw_url. */
audd_error_t audd_url_append_return(const char *raw_url,
                                    const char **return_metadata,
                                    char **out)
{
    if (out == NULL) return AUDD_ERR_INVALID_ARGUMENT;
    *out = NULL;
    if (raw_url == NULL) return AUDD_ERR_INVALID_ARGUMENT;

    /* Empty / NULL list → return URL unchanged. */
    int has_any = 0;
    if (return_metadata != NULL) {
        for (size_t i = 0; return_metadata[i] != NULL; ++i) {
            if (return_metadata[i][0] != '\0') {
                has_any = 1;
                break;
            }
        }
    }
    if (!has_any) {
        *out = audd_strdup(raw_url);
        return *out != NULL ? AUDD_OK : AUDD_ERR_OUT_OF_MEMORY;
    }

    /* Scan for an existing return= param. We look at everything after '?'. */
    const char *q = strchr(raw_url, '?');
    if (q != NULL) {
        const char *p = q + 1;
        while (*p != '\0' && *p != '#') {
            const char *amp = strchr(p, '&');
            const char *end = (amp != NULL) ? amp : p + strlen(p);
            const char *eq = memchr(p, '=', (size_t)(end - p));
            const char *key_end = (eq != NULL) ? eq : end;
            size_t klen = (size_t)(key_end - p);
            if (klen == 6 && memcmp(p, "return", 6) == 0) {
                return AUDD_ERR_INVALID_ARGUMENT;
            }
            if (amp == NULL) break;
            p = amp + 1;
        }
    }

    /* Build CSV value. */
    size_t total = 0;
    size_t count = 0;
    for (size_t i = 0; return_metadata[i] != NULL; ++i) {
        if (return_metadata[i][0] == '\0') continue;
        total += strlen(return_metadata[i]) + 1;
        ++count;
    }
    if (count == 0 || total == 0) {
        *out = audd_strdup(raw_url);
        return *out != NULL ? AUDD_OK : AUDD_ERR_OUT_OF_MEMORY;
    }
    char *csv = (char *)audd_malloc(total);
    if (csv == NULL) return AUDD_ERR_OUT_OF_MEMORY;
    size_t off = 0;
    int first = 1;
    for (size_t i = 0; return_metadata[i] != NULL; ++i) {
        if (return_metadata[i][0] == '\0') continue;
        if (!first) csv[off++] = ',';
        first = 0;
        size_t l = strlen(return_metadata[i]);
        memcpy(csv + off, return_metadata[i], l);
        off += l;
    }
    csv[off] = '\0';

    char *enc = url_encode(csv);
    audd_free(csv);
    if (enc == NULL) return AUDD_ERR_OUT_OF_MEMORY;

    char sep = (q == NULL) ? '?' : '&';
    char *result = audd_aprintf("%s%creturn=%s", raw_url, sep, enc);
    audd_free(enc);
    if (result == NULL) return AUDD_ERR_OUT_OF_MEMORY;
    *out = result;
    return AUDD_OK;
}
