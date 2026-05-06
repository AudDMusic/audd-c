/* audd_alloc.c — global allocator hook + small helpers. */
#include "audd_internal.h"
#include "audd.h"
#include "../include/audd_version.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../vendor/cJSON/cJSON.h"

static audd_allocator_t g_alloc = {0};
static int g_alloc_set = 0;

void audd_set_allocator(const audd_allocator_t *alloc)
{
    if (alloc == NULL) {
        g_alloc_set = 0;
        memset(&g_alloc, 0, sizeof(g_alloc));
        cJSON_InitHooks(NULL);
        return;
    }
    g_alloc = *alloc;
    g_alloc_set = 1;

    /* Wire cJSON to use the same hooks. */
    cJSON_Hooks hooks;
    hooks.malloc_fn = alloc->malloc_fn;
    hooks.free_fn = alloc->free_fn;
    cJSON_InitHooks(&hooks);
}

void *audd_malloc(size_t size)
{
    if (g_alloc_set && g_alloc.malloc_fn) {
        return g_alloc.malloc_fn(size);
    }
    return malloc(size);
}

void audd_free(void *ptr)
{
    if (ptr == NULL) {
        return;
    }
    if (g_alloc_set && g_alloc.free_fn) {
        g_alloc.free_fn(ptr);
        return;
    }
    free(ptr);
}

void *audd_realloc(void *ptr, size_t size)
{
    if (g_alloc_set && g_alloc.realloc_fn) {
        return g_alloc.realloc_fn(ptr, size);
    }
    if (g_alloc_set && g_alloc.malloc_fn) {
        /* No realloc plugged in; emulate. */
        if (size == 0) {
            audd_free(ptr);
            return NULL;
        }
        void *p = audd_malloc(size);
        if (p == NULL) {
            return NULL;
        }
        if (ptr != NULL) {
            /* We don't know old size; the caller of audd_realloc must
             * tolerate this fallback by tracking growth themselves. We
             * only use realloc with a known-tracked buffer in this SDK. */
            memcpy(p, ptr, size);
            audd_free(ptr);
        }
        return p;
    }
    return realloc(ptr, size);
}

char *audd_strdup(const char *s)
{
    if (s == NULL) {
        return NULL;
    }
    size_t n = strlen(s);
    char *out = (char *)audd_malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, s, n + 1);
    return out;
}

char *audd_strndup(const char *s, size_t n)
{
    if (s == NULL) {
        return NULL;
    }
    char *out = (char *)audd_malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

char *audd_aprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        va_end(ap2);
        return NULL;
    }
    char *buf = (char *)audd_malloc((size_t)n + 1);
    if (buf == NULL) {
        va_end(ap2);
        return NULL;
    }
    vsnprintf(buf, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return buf;
}

void audd_str_clear(audd_str_t *s)
{
    if (s == NULL) return;
    audd_free(s->data);
    s->data = NULL;
    s->len = 0;
}

int audd_str_set(audd_str_t *s, const char *src, size_t len)
{
    if (s == NULL) return -1;
    audd_free(s->data);
    if (src == NULL) {
        s->data = NULL;
        s->len = 0;
        return 0;
    }
    s->data = (char *)audd_malloc(len + 1);
    if (s->data == NULL) {
        s->len = 0;
        return -1;
    }
    memcpy(s->data, src, len);
    s->data[len] = '\0';
    s->len = len;
    return 0;
}

const char *audd_version(void)
{
    return AUDD_VERSION;
}
