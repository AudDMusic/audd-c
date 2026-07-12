/* test_enterprise_oom.c — enterprise result allocation failure is reported.
 *
 * audd_enterprise_from_json returns NULL only on out-of-memory. The recognize
 * path relies on that to return AUDD_ERR_OUT_OF_MEMORY instead of a bogus
 * "success with 0 matches". Here we drive the NULL-on-OOM contract with a
 * failing allocator. */
#include "audd.h"
#include "audd_internal.h"
#include "unity.h"

#include "../vendor/cJSON/cJSON.h"

#include <stdlib.h>

/* Fail the Nth (1-based) audd_malloc; count down from g_fail_at. */
static int g_fail_at;   /* 0 = never fail */
static int g_calls;

static void *counting_malloc(size_t n)
{
    g_calls++;
    if (g_fail_at != 0 && g_calls >= g_fail_at) return NULL;
    return malloc(n);
}
static void counting_free(void *p) { free(p); }

void setUp(void)
{
    g_fail_at = 0;
    g_calls = 0;
}
void tearDown(void)
{
    audd_set_allocator(NULL);
}

void test_enterprise_from_json_null_on_oom(void)
{
    /* Parse with the default allocator first. */
    const char *json = "[{\"songs\":[{\"score\":100,\"artist\":\"A\",\"title\":\"T\"}],\"offset\":\"0:00\"}]";
    cJSON *obj = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(obj);

    /* Now install a failing allocator: the very first SDK allocation (the
     * result container) fails. */
    audd_allocator_t a = { .malloc_fn = counting_malloc, .free_fn = counting_free, .realloc_fn = NULL };
    audd_set_allocator(&a);
    g_fail_at = 1;

    audd_enterprise_result_t *r = audd_enterprise_from_json(obj);
    TEST_ASSERT_NULL_MESSAGE(r, "enterprise parse must return NULL on OOM, not an empty result");

    audd_set_allocator(NULL);
    cJSON_Delete(obj);
}

/* When allocation succeeds, a non-array/empty result yields a valid, empty
 * container (never NULL) — so NULL unambiguously means OOM. */
void test_enterprise_from_json_empty_is_not_null(void)
{
    cJSON *obj = cJSON_Parse("[]");
    TEST_ASSERT_NOT_NULL(obj);
    audd_enterprise_result_t *r = audd_enterprise_from_json(obj);
    cJSON_Delete(obj);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_INT(0, (int)audd_enterprise_result_count(r));
    audd_enterprise_result_free(r);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_enterprise_from_json_null_on_oom);
    RUN_TEST(test_enterprise_from_json_empty_is_not_null);
    return UNITY_END();
}
