/* test_longpoll_category.c — DeriveLongpollCategory parity. */
#include "audd.h"
#include "unity.h"

#include <string.h>
#include <ctype.h>

void setUp(void) {}
void tearDown(void) {}

void test_stable_and_correct_length(void)
{
    char a[10], b[10], c[10];
    audd_error_t e1 = audd_derive_longpoll_category("test", 7, a);
    audd_error_t e2 = audd_derive_longpoll_category("test", 7, b);
    audd_error_t e3 = audd_derive_longpoll_category("test", 8, c);
    TEST_ASSERT_EQUAL(AUDD_OK, e1);
    TEST_ASSERT_EQUAL(AUDD_OK, e2);
    TEST_ASSERT_EQUAL(AUDD_OK, e3);
    TEST_ASSERT_EQUAL_INT(9, (int)strlen(a));
    TEST_ASSERT_EQUAL_STRING(a, b);
    TEST_ASSERT_NOT_EQUAL(0, strcmp(a, c));
}

void test_only_hex_chars(void)
{
    char cat[10];
    TEST_ASSERT_EQUAL(AUDD_OK, audd_derive_longpoll_category("test", 7, cat));
    for (int i = 0; i < 9; ++i) {
        TEST_ASSERT_TRUE_MESSAGE(
            (cat[i] >= '0' && cat[i] <= '9') || (cat[i] >= 'a' && cat[i] <= 'f'),
            "unexpected non-hex char in longpoll category");
    }
}

void test_different_tokens_different_outputs(void)
{
    char a[10], b[10];
    TEST_ASSERT_EQUAL(AUDD_OK, audd_derive_longpoll_category("test",  7, a));
    TEST_ASSERT_EQUAL(AUDD_OK, audd_derive_longpoll_category("other", 7, b));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(a, b));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_stable_and_correct_length);
    RUN_TEST(test_only_hex_chars);
    RUN_TEST(test_different_tokens_different_outputs);
    return UNITY_END();
}
