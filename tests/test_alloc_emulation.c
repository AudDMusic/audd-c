/* test_alloc_emulation.c — realloc emulation for malloc-only custom allocators.
 *
 * When a caller installs a custom allocator that supplies malloc/free but no
 * realloc, the SDK emulates realloc. It must copy only the still-valid bytes
 * (min(old, new)) — copying the NEW size read past the old allocation. Under
 * ASan the pre-fix code trips a heap-buffer-overflow here. */
#include "audd.h"
#include "audd_internal.h"
#include "unity.h"

#include <stdlib.h>
#include <string.h>

/* A malloc-only allocator. We deliberately provide no realloc_fn so the SDK
 * takes the emulation path. Counters let us assert the hooks are used. */
static size_t g_malloc_calls;
static size_t g_free_calls;

static void *test_malloc(size_t n) { g_malloc_calls++; return malloc(n); }
static void  test_free(void *p)    { if (p) g_free_calls++; free(p); }

void setUp(void)
{
    g_malloc_calls = 0;
    g_free_calls = 0;
    audd_allocator_t a = { .malloc_fn = test_malloc, .free_fn = test_free, .realloc_fn = NULL };
    audd_set_allocator(&a);
}

void tearDown(void)
{
    audd_set_allocator(NULL);
}

/* Grow a buffer through the emulated realloc path. The old contents must be
 * preserved and no read may run past the previous (smaller) allocation. */
void test_emulated_realloc_grows_without_oob(void)
{
    size_t small = 8;
    char *p = (char *)audd_realloc(NULL, small);
    TEST_ASSERT_NOT_NULL(p);
    memset(p, 'A', small);

    /* Grow well past the original size; emulation copies min(old,new)=8 bytes.
     * Pre-fix it copied the NEW size (4096), reading OOB. */
    size_t big = 4096;
    char *q = (char *)audd_realloc(p, big);
    TEST_ASSERT_NOT_NULL(q);
    for (size_t i = 0; i < small; ++i) {
        TEST_ASSERT_EQUAL_UINT8('A', (unsigned char)q[i]);
    }
    /* Writing the whole grown region must be in-bounds. */
    memset(q, 'B', big);

    audd_realloc_free(q);
    TEST_ASSERT_TRUE(g_malloc_calls >= 2);
}

/* Shrinking must also stay in bounds and keep the head bytes. */
void test_emulated_realloc_shrinks(void)
{
    size_t big = 1024;
    char *p = (char *)audd_realloc(NULL, big);
    TEST_ASSERT_NOT_NULL(p);
    memset(p, 'C', big);

    char *q = (char *)audd_realloc(p, 16);
    TEST_ASSERT_NOT_NULL(q);
    for (size_t i = 0; i < 16; ++i) {
        TEST_ASSERT_EQUAL_UINT8('C', (unsigned char)q[i]);
    }
    audd_realloc_free(q);
}

/* Freeing via size==0 releases the emulated block cleanly. */
void test_emulated_realloc_free_via_zero(void)
{
    char *p = (char *)audd_realloc(NULL, 32);
    TEST_ASSERT_NOT_NULL(p);
    void *r = audd_realloc(p, 0);
    TEST_ASSERT_NULL(r);
    TEST_ASSERT_TRUE(g_free_calls >= 1);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_emulated_realloc_grows_without_oob);
    RUN_TEST(test_emulated_realloc_shrinks);
    RUN_TEST(test_emulated_realloc_free_via_zero);
    return UNITY_END();
}
