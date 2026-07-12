/* test_post_upload_timeout.c — post-upload timeouts must not be retried.
 *
 * Uses a local TCP server: it reads the full multipart request body, then
 * never answers, so the client times out AFTER the upload completed. The
 * server may already have done (and billed) the metered work, so
 * audd_http_post must report body_was_uploaded=1 and the recognition retry
 * class must make exactly one attempt. A connect-refused failure, by
 * contrast, provably never sent the body and stays retryable. */
#include "audd.h"
#include "audd_internal.h"
#include "unity.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

void setUp(void) {}
void tearDown(void) {}

/* ------------------------------------------------------------------ *
 * Local server: accepts connections, reads the full HTTP request      *
 * (headers + Content-Length body bytes), then reads until the client  *
 * gives up and closes. Never sends a response.                        *
 * ------------------------------------------------------------------ */

typedef struct {
    int          listen_fd;
    volatile int full_bodies_received; /* full request bodies consumed */
    volatile int stop;
} server_t;

/* Case-insensitive search for header `name` (include the trailing ':') in
 * the header block; returns a pointer just past the ':' or NULL. */
static const char *find_header(const char *hdrs, size_t len, const char *name)
{
    size_t klen = strlen(name);
    for (size_t i = 0; i + klen <= len; ++i) {
        if (strncasecmp(hdrs + i, name, klen) == 0) {
            return hdrs + i + klen;
        }
    }
    return NULL;
}

/* Read one request off `conn`: headers, then Content-Length body bytes.
 * Returns 0 when the full body was consumed, -1 otherwise. */
static int read_full_request(int conn)
{
    char   buf[65536];
    size_t got = 0;
    size_t hdr_end = 0;

    /* Headers. */
    while (hdr_end == 0 && got < sizeof(buf) - 1) {
        ssize_t n = read(conn, buf + got, sizeof(buf) - 1 - got);
        if (n <= 0) return -1;
        got += (size_t)n;
        buf[got] = '\0';
        const char *p = strstr(buf, "\r\n\r\n");
        if (p != NULL) hdr_end = (size_t)(p - buf) + 4;
    }
    if (hdr_end == 0) return -1;

    const char *clv = find_header(buf, hdr_end, "content-length:");
    if (clv == NULL) return -1;
    long content_length = strtol(clv, NULL, 10);
    if (content_length < 0) return -1;

    /* An old libcurl may pause a large upload behind "Expect: 100-continue";
     * acknowledge it so the body always flows. */
    if (find_header(buf, hdr_end, "expect:") != NULL) {
        static const char cont[] = "HTTP/1.1 100 Continue\r\n\r\n";
        if (write(conn, cont, sizeof(cont) - 1) < 0) return -1;
    }

    /* Body. */
    long body_got = (long)(got - hdr_end);
    while (body_got < content_length) {
        char sink[8192];
        size_t want = sizeof(sink);
        if ((long)want > content_length - body_got) {
            want = (size_t)(content_length - body_got);
        }
        ssize_t n = read(conn, sink, want);
        if (n <= 0) return -1;
        body_got += (long)n;
    }
    return 0;
}

static void *server_main(void *arg)
{
    server_t *srv = (server_t *)arg;
    for (;;) {
        int conn = accept(srv->listen_fd, NULL, NULL);
        if (conn < 0) break;
        if (srv->stop) {
            close(conn);
            break;
        }
        if (read_full_request(conn) == 0) {
            srv->full_bodies_received += 1;
            /* Hang: send nothing; drain until the client times out and
             * closes the connection. */
            char sink[4096];
            while (read(conn, sink, sizeof(sink)) > 0) {}
        }
        close(conn);
    }
    return NULL;
}

/* Bind 127.0.0.1:0, listen; returns the fd and writes the port. */
static int listen_on_ephemeral_port(int *port_out)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT_TRUE_MESSAGE(fd >= 0, "socket() failed");
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, bind(fd, (struct sockaddr *)&addr, sizeof(addr)),
                                  "bind() failed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, listen(fd, 8), "listen() failed");

    socklen_t alen = sizeof(addr);
    TEST_ASSERT_EQUAL_INT(0, getsockname(fd, (struct sockaddr *)&addr, &alen));
    *port_out = (int)ntohs(addr.sin_port);
    return fd;
}

/* ------------------------------------------------------------------ *
 * Client-side plumbing.                                               *
 * ------------------------------------------------------------------ */

static audd_client_t *make_client(void)
{
    audd_options_t o = audd_options_default();
    o.max_attempts = 3;
    o.backoff_ms = 1;
    audd_client_t *c = audd_client_new("dummy-token", &o);
    TEST_ASSERT_NOT_NULL(c);
    return c;
}

typedef struct {
    const char *url;
    int         attempts;
    int         last_body_uploaded;
} post_ctx_t;

/* One real HTTP attempt: multipart POST of an 8 KiB payload. */
static int post_attempt(audd_client_t *client,
                        audd_http_response_t *resp,
                        int *body_was_uploaded,
                        void *ud)
{
    post_ctx_t *ctx = (post_ctx_t *)ud;
    ctx->attempts += 1;

    static unsigned char payload[8192];
    memset(payload, 0x5a, sizeof(payload));
    audd_http_file_t file = {0};
    file.name = "file";
    file.filename = "clip.wav";
    file.content_type = "audio/wav";
    file.data = payload;
    file.size = sizeof(payload);
    const char *fields[] = { "k", "v", NULL };
    audd_http_form_t form = { fields, &file };

    int rc = audd_http_post(client, ctx->url, &form, 2 /*s*/, resp, body_was_uploaded);
    if (body_was_uploaded) ctx->last_body_uploaded = *body_was_uploaded;
    return rc;
}

/* Server fully receives the multipart body, then never answers within the
 * client timeout. The failure is post-upload, so the recognition retry class
 * must NOT re-send the upload: exactly one attempt, and the server saw the
 * body exactly once. */
void test_timeout_after_upload_one_attempt(void)
{
    int port = 0;
    server_t srv = {0};
    srv.listen_fd = listen_on_ephemeral_port(&port);
    pthread_t th;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&th, NULL, server_main, &srv));

    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/", port);

    audd_client_t *c = make_client();
    post_ctx_t ctx = { url, 0, -1 };
    audd_http_response_t resp = {0};
    int rc = audd_retry_do(c, AUDD_RETRY_RECOGNITION, post_attempt, &ctx, &resp);

    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, rc, "the timed-out attempt must fail");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, ctx.last_body_uploaded,
        "timeout after the body was uploaded must report body_was_uploaded=1");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, ctx.attempts,
        "a post-upload timeout must not be retried; expected exactly 1 attempt");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, srv.full_bodies_received,
        "the server must have seen the upload exactly once");

    audd_http_response_free(&resp);
    audd_client_free(c);

    /* Wake the accept loop and join. */
    srv.stop = 1;
    int wake = socket(AF_INET, SOCK_STREAM, 0);
    if (wake >= 0) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons((unsigned short)port);
        (void)connect(wake, (struct sockaddr *)&addr, sizeof(addr));
        close(wake);
    }
    pthread_join(th, NULL);
    close(srv.listen_fd);
}

/* Connect refused: the body was never sent, so the failure is pre-upload and
 * the recognition retry class keeps retrying it up to max_attempts. */
void test_connect_refused_pre_upload_and_retried(void)
{
    /* Grab an ephemeral port, then close the listener so connects to it are
     * refused. */
    int port = 0;
    int fd = listen_on_ephemeral_port(&port);
    close(fd);

    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/", port);

    audd_client_t *c = make_client();
    post_ctx_t ctx = { url, 0, -1 };
    audd_http_response_t resp = {0};
    int rc = audd_retry_do(c, AUDD_RETRY_RECOGNITION, post_attempt, &ctx, &resp);

    TEST_ASSERT_EQUAL_INT(-1, rc);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, ctx.last_body_uploaded,
        "connect-refused must report body_was_uploaded=0 (pre-upload)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, ctx.attempts,
        "pre-upload failures stay retryable; expected max_attempts (=3)");

    audd_http_response_free(&resp);
    audd_client_free(c);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_timeout_after_upload_one_attempt);
    RUN_TEST(test_connect_refused_pre_upload_and_retried);
    return UNITY_END();
}
