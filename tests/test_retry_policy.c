/* test_retry_policy.c — verify retry classification.
 *
 * Custom-catalog upload is metered; auto-retry could double-charge. The
 * audd_custom_catalog_add* functions use AUDD_RETRY_NONE, which guarantees
 * exactly one attempt regardless of outcome. These tests exercise the
 * underlying audd_retry_do runner with a fake attempt function so we can
 * assert the attempt counter without hitting the network. */
#include "audd.h"
#include "audd_internal.h"
#include "unity.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Shared counter + scripted outcome for the fake attempt fn. */
typedef struct {
    int  calls;
    int  return_rc;       /* value the attempt fn returns */
    long set_status;      /* HTTP status to write into resp */
    int  set_body_upload; /* value to write into *body_was_uploaded */
} fake_t;

static int fake_attempt(audd_client_t *client,
                         audd_http_response_t *resp,
                         int *body_was_uploaded,
                         void *ud)
{
    (void)client;
    fake_t *f = (fake_t *)ud;
    f->calls += 1;
    resp->status = f->set_status;
    if (body_was_uploaded) *body_was_uploaded = f->set_body_upload;
    return f->return_rc;
}

/* Build a client with backoff_ms=1 so multi-attempt cases don't sleep
 * meaningfully. (We're asserting the call count is 1, so it shouldn't
 * actually loop — but if it did regress to 3 we'd rather not wait 1.5s.) */
static audd_client_t *make_client(void)
{
    audd_options_t o = audd_options_default();
    o.max_attempts = 3;
    o.backoff_ms = 1;
    audd_client_t *c = audd_client_new("dummy-token", &o);
    TEST_ASSERT_NOT_NULL(c);
    return c;
}

void test_none_class_one_attempt_on_5xx(void)
{
    audd_client_t *c = make_client();
    fake_t f = { .calls = 0, .return_rc = 0, .set_status = 503, .set_body_upload = 1 };
    audd_http_response_t resp = {0};
    int rc = audd_retry_do(c, AUDD_RETRY_NONE, fake_attempt, &f, &resp);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, f.calls,
        "AUDD_RETRY_NONE must not retry on 5xx; expected exactly 1 attempt");
    audd_http_response_free(&resp);
    audd_client_free(c);
}

void test_none_class_one_attempt_on_pre_upload_connect_error(void)
{
    audd_client_t *c = make_client();
    /* rc=-1 (connection error), body_uploaded=0 (pre-upload). */
    fake_t f = { .calls = 0, .return_rc = -1, .set_status = 0, .set_body_upload = 0 };
    audd_http_response_t resp = {0};
    int rc = audd_retry_do(c, AUDD_RETRY_NONE, fake_attempt, &f, &resp);
    TEST_ASSERT_EQUAL_INT(-1, rc);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, f.calls,
        "AUDD_RETRY_NONE must not retry on pre-upload connect error; expected exactly 1 attempt");
    audd_http_response_free(&resp);
    audd_client_free(c);
}

void test_none_class_one_attempt_on_post_upload_connect_error(void)
{
    audd_client_t *c = make_client();
    /* Even when body was uploaded (post-upload connect error), AUDD_RETRY_NONE
     * should still be one-shot. */
    fake_t f = { .calls = 0, .return_rc = -1, .set_status = 0, .set_body_upload = 1 };
    audd_http_response_t resp = {0};
    int rc = audd_retry_do(c, AUDD_RETRY_NONE, fake_attempt, &f, &resp);
    TEST_ASSERT_EQUAL_INT(-1, rc);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, f.calls,
        "AUDD_RETRY_NONE must not retry on post-upload connect error either");
    audd_http_response_free(&resp);
    audd_client_free(c);
}

/* Sanity: AUDD_RETRY_READ still retries on 5xx (regression guard). */
void test_read_class_still_retries_on_5xx(void)
{
    audd_client_t *c = make_client();
    fake_t f = { .calls = 0, .return_rc = 0, .set_status = 503, .set_body_upload = 1 };
    audd_http_response_t resp = {0};
    (void)audd_retry_do(c, AUDD_RETRY_READ, fake_attempt, &f, &resp);
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, f.calls,
        "AUDD_RETRY_READ should retry on 5xx up to max_attempts (=3)");
    audd_http_response_free(&resp);
    audd_client_free(c);
}

/* Sanity: AUDD_RETRY_MUTATING retries pre-upload connect errors. */
void test_mutating_class_retries_pre_upload(void)
{
    audd_client_t *c = make_client();
    fake_t f = { .calls = 0, .return_rc = -1, .set_status = 0, .set_body_upload = 0 };
    audd_http_response_t resp = {0};
    (void)audd_retry_do(c, AUDD_RETRY_MUTATING, fake_attempt, &f, &resp);
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, f.calls,
        "AUDD_RETRY_MUTATING should retry pre-upload connect errors");
    audd_http_response_free(&resp);
    audd_client_free(c);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_none_class_one_attempt_on_5xx);
    RUN_TEST(test_none_class_one_attempt_on_pre_upload_connect_error);
    RUN_TEST(test_none_class_one_attempt_on_post_upload_connect_error);
    RUN_TEST(test_read_class_still_retries_on_5xx);
    RUN_TEST(test_mutating_class_retries_pre_upload);
    return UNITY_END();
}
