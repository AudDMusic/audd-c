/* test_pre_upload_classification.c — pre-upload failure classification.
 *
 * Recognition and mutating requests are metered; the retry layer may only
 * re-send them when the failure provably happened before any request-body
 * byte reached the server. audd_failure_is_pre_upload gates that decision:
 * unambiguous pre-transfer CURLcodes (DNS, connect, TLS handshake, malformed
 * URL) are always pre-upload, while ambiguous codes (timeouts, send/recv
 * errors) count as pre-upload only when the curl probes show zero uploaded
 * body bytes AND no HTTP status line. These tests pin the table down. */
#include "audd.h"
#include "audd_internal.h"
#include "unity.h"

#include <curl/curl.h>

void setUp(void) {}
void tearDown(void) {}

/* Codes that can only fire before the transfer starts are pre-upload
 * regardless of the probes. */
void test_pre_transfer_codes_are_pre_upload(void)
{
    TEST_ASSERT_EQUAL_INT(1, audd_failure_is_pre_upload(CURLE_UNSUPPORTED_PROTOCOL, 0, 0));
    TEST_ASSERT_EQUAL_INT(1, audd_failure_is_pre_upload(CURLE_URL_MALFORMAT, 0, 0));
    TEST_ASSERT_EQUAL_INT(1, audd_failure_is_pre_upload(CURLE_COULDNT_RESOLVE_PROXY, 0, 0));
    TEST_ASSERT_EQUAL_INT(1, audd_failure_is_pre_upload(CURLE_COULDNT_RESOLVE_HOST, 0, 0));
    TEST_ASSERT_EQUAL_INT(1, audd_failure_is_pre_upload(CURLE_COULDNT_CONNECT, 0, 0));
    TEST_ASSERT_EQUAL_INT(1, audd_failure_is_pre_upload(CURLE_SSL_CONNECT_ERROR, 0, 0));
    TEST_ASSERT_EQUAL_INT(1, audd_failure_is_pre_upload(CURLE_PEER_FAILED_VERIFICATION, 0, 0));
    TEST_ASSERT_EQUAL_INT(1, audd_failure_is_pre_upload(CURLE_SSL_CIPHER, 0, 0));
    TEST_ASSERT_EQUAL_INT(1, audd_failure_is_pre_upload(CURLE_SSL_CACERT_BADFILE, 0, 0));
    TEST_ASSERT_EQUAL_INT(1, audd_failure_is_pre_upload(CURLE_INTERFACE_FAILED, 0, 0));
}

/* A timeout is ambiguous: pre-upload only when the probes prove no body byte
 * was sent and no status line came back. */
void test_timeout_with_clean_probes_is_pre_upload(void)
{
    TEST_ASSERT_EQUAL_INT(1, audd_failure_is_pre_upload(CURLE_OPERATION_TIMEDOUT, 0, 0));
}

/* Timed out AFTER the body was uploaded — the server may already have done
 * (and billed) the metered work. Must NOT classify as pre-upload. */
void test_timeout_after_upload_is_not_pre_upload(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(0,
        audd_failure_is_pre_upload(CURLE_OPERATION_TIMEDOUT, 48213, 0),
        "timeout with uploaded body bytes must not be pre-upload");
}

/* A status line arrived, so the request reached the server's app layer. */
void test_timeout_with_status_line_is_not_pre_upload(void)
{
    TEST_ASSERT_EQUAL_INT(0, audd_failure_is_pre_upload(CURLE_OPERATION_TIMEDOUT, 48213, 200));
    TEST_ASSERT_EQUAL_INT(0, audd_failure_is_pre_upload(CURLE_OPERATION_TIMEDOUT, 0, 200));
}

/* Other ambiguous codes follow the same probe rule. */
void test_other_ambiguous_codes_follow_probes(void)
{
    TEST_ASSERT_EQUAL_INT(1, audd_failure_is_pre_upload(CURLE_SEND_ERROR, 0, 0));
    TEST_ASSERT_EQUAL_INT(0, audd_failure_is_pre_upload(CURLE_SEND_ERROR, 512, 0));
    TEST_ASSERT_EQUAL_INT(0, audd_failure_is_pre_upload(CURLE_RECV_ERROR, 48213, 0));
    TEST_ASSERT_EQUAL_INT(0, audd_failure_is_pre_upload(CURLE_PARTIAL_FILE, 48213, 200));
    TEST_ASSERT_EQUAL_INT(0, audd_failure_is_pre_upload(CURLE_GOT_NOTHING, 48213, 0));
    TEST_ASSERT_EQUAL_INT(0, audd_failure_is_pre_upload(CURLE_ABORTED_BY_CALLBACK, 100, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pre_transfer_codes_are_pre_upload);
    RUN_TEST(test_timeout_with_clean_probes_is_pre_upload);
    RUN_TEST(test_timeout_after_upload_is_not_pre_upload);
    RUN_TEST(test_timeout_with_status_line_is_not_pre_upload);
    RUN_TEST(test_other_ambiguous_codes_follow_probes);
    return UNITY_END();
}
