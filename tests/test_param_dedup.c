/* test_param_dedup.c — typed params shadow extra_parameters.
 *
 * audd.h documents that typed request params win over any extras entry with
 * the same key. These tests exercise the request-field assembler directly and
 * assert no key is sent twice. */
#include "audd.h"
#include "audd_internal.h"
#include "unity.h"

#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Count how many times `key` appears as a field name in an assembled,
 * NULL-terminated alternating key/value array. */
static int count_key(const char **fields, const char *key)
{
    int n = 0;
    for (size_t i = 0; fields[i] != NULL && fields[i + 1] != NULL; i += 2) {
        if (strcmp(fields[i], key) == 0) n++;
    }
    return n;
}

static const char *value_for(const char **fields, const char *key)
{
    for (size_t i = 0; fields[i] != NULL && fields[i + 1] != NULL; i += 2) {
        if (strcmp(fields[i], key) == 0) return fields[i + 1];
    }
    return NULL;
}

/* A typed `return` plus an extras `return` must collapse to one `return`,
 * carrying the typed value. */
void test_typed_return_shadows_extras_return(void)
{
    const char *extras[] = { "return", "spotify", "custom", "keep", NULL };
    audd_recognize_ctx_t ctx = {0};
    ctx.return_csv = "apple_music,spotify";
    ctx.extra_parameters = extras;

    size_t cap = audd_recognize_fields_capacity(&ctx);
    const char **fields = calloc(cap, sizeof(*fields));
    TEST_ASSERT_NOT_NULL(fields);
    audd_recognize_scratch_t scratch;
    audd_build_recognize_fields(&ctx, &scratch, fields, cap);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_key(fields, "return"),
        "exactly one `return` field expected");
    TEST_ASSERT_EQUAL_STRING("apple_music,spotify", value_for(fields, "return"));
    /* Non-colliding extras survive. */
    TEST_ASSERT_EQUAL_INT(1, count_key(fields, "custom"));
    TEST_ASSERT_EQUAL_STRING("keep", value_for(fields, "custom"));
    free(fields);
}

/* Enterprise numeric typed field (limit) shadows an extras `limit`. */
void test_typed_limit_shadows_extras_limit(void)
{
    const char *extras[] = { "limit", "999", "market", "gb", NULL };
    audd_enterprise_options_t eo = audd_enterprise_options_default();
    eo.limit = 3;
    audd_recognize_ctx_t ctx = {0};
    ctx.is_enterprise = 1;
    ctx.eopts = &eo;
    ctx.extra_parameters = extras;

    size_t cap = audd_recognize_fields_capacity(&ctx);
    const char **fields = calloc(cap, sizeof(*fields));
    TEST_ASSERT_NOT_NULL(fields);
    audd_recognize_scratch_t scratch;
    audd_build_recognize_fields(&ctx, &scratch, fields, cap);

    TEST_ASSERT_EQUAL_INT(1, count_key(fields, "limit"));
    TEST_ASSERT_EQUAL_STRING("3", value_for(fields, "limit"));
    /* market not a typed field here, so the extras entry passes through. */
    TEST_ASSERT_EQUAL_INT(1, count_key(fields, "market"));
    free(fields);
}

/* Extras with no typed collision are all preserved. */
void test_extras_without_collision_preserved(void)
{
    const char *extras[] = { "a", "1", "b", "2", NULL };
    audd_recognize_ctx_t ctx = {0};
    ctx.extra_parameters = extras;

    size_t cap = audd_recognize_fields_capacity(&ctx);
    const char **fields = calloc(cap, sizeof(*fields));
    TEST_ASSERT_NOT_NULL(fields);
    audd_recognize_scratch_t scratch;
    audd_build_recognize_fields(&ctx, &scratch, fields, cap);

    TEST_ASSERT_EQUAL_INT(1, count_key(fields, "a"));
    TEST_ASSERT_EQUAL_INT(1, count_key(fields, "b"));
    free(fields);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_typed_return_shadows_extras_return);
    RUN_TEST(test_typed_limit_shadows_extras_limit);
    RUN_TEST(test_extras_without_collision_preserved);
    return UNITY_END();
}
