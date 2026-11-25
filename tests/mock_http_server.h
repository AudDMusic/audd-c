/* mock_http_server.h — a scripted HTTP/1.1 server on 127.0.0.1 for tests.
 *
 * Each connection carries one request. The server records the request (head
 * plus up to MOCK_REQ_MAX bytes in all of its Content-Length body), then
 * answers with the next scripted response.
 * Past the end of the script it either repeats the last response or, with
 * hold_after_script, keeps the connection open without answering until the
 * client gives up or the server stops. Every wait is bounded. */
#ifndef AUDD_MOCK_HTTP_SERVER_H
#define AUDD_MOCK_HTTP_SERVER_H

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define MOCK_MAX_SCRIPT   16
#define MOCK_MAX_REQUESTS 256
#define MOCK_REQ_MAX      4096

typedef struct {
    int         status;
    const char *body;
} mock_response_t;

typedef struct {
    int             listen_fd;
    int             port;
    pthread_t       thread;
    volatile int    stop;
    pthread_mutex_t mu;

    mock_response_t script[MOCK_MAX_SCRIPT];
    int             nscript;
    int             hold_after_script;

    int  nrequests;                            /* guarded by mu */
    char reqs[MOCK_MAX_REQUESTS][MOCK_REQ_MAX];
} mock_server_t;

static long mock_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void mock_sleep_ms(long ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static int mock_request_count(mock_server_t *s)
{
    pthread_mutex_lock(&s->mu);
    int n = s->nrequests;
    pthread_mutex_unlock(&s->mu);
    return n;
}

/* Read the request: the head, then its Content-Length body. Keeps the
 * first cap-1 bytes in buf (NUL-terminated). 0 on success. */
static int mock_read_request(mock_server_t *s, int conn, char *buf, size_t cap)
{
    size_t got = 0;
    long deadline = mock_now_ms() + 3000;
    const char *end_of_head = NULL;
    while (end_of_head == NULL) {
        if (got + 1 >= cap || s->stop || mock_now_ms() >= deadline) return -1;
        struct pollfd p = { conn, POLLIN, 0 };
        if (poll(&p, 1, 50) <= 0) continue;
        ssize_t n = read(conn, buf + got, cap - 1 - got);
        if (n <= 0) return -1;
        got += (size_t)n;
        buf[got] = '\0';
        end_of_head = strstr(buf, "\r\n\r\n");
    }
    size_t head_len = (size_t)(end_of_head - buf) + 4;
    long content_length = 0;
    for (const char *q = buf; q < buf + head_len; ++q) {
        if (strncasecmp(q, "\r\ncontent-length:", 17) == 0) {
            content_length = strtol(q + 17, NULL, 10);
            break;
        }
    }
    long body_got = (long)(got - head_len);
    while (body_got < content_length && !s->stop && mock_now_ms() < deadline) {
        struct pollfd p = { conn, POLLIN, 0 };
        if (poll(&p, 1, 50) <= 0) continue;
        char sink[4096];
        char *dst = got + 1 < cap ? buf + got : sink;
        size_t room = got + 1 < cap ? cap - 1 - got : sizeof(sink);
        ssize_t n = read(conn, dst, room);
        if (n <= 0) return -1;
        if (dst != sink) {
            got += (size_t)n;
            buf[got] = '\0';
        }
        body_got += (long)n;
    }
    return body_got >= content_length ? 0 : -1;
}

static void mock_write_all(int conn, const char *p, size_t n)
{
    while (n > 0) {
        ssize_t w = write(conn, p, n);
        if (w <= 0) return;
        p += w;
        n -= (size_t)w;
    }
}

static void *mock_server_main(void *arg)
{
    mock_server_t *s = (mock_server_t *)arg;
    while (!s->stop) {
        struct pollfd p = { s->listen_fd, POLLIN, 0 };
        if (poll(&p, 1, 50) <= 0) continue;
        int conn = accept(s->listen_fd, NULL, NULL);
        if (conn < 0) continue;

        char req[MOCK_REQ_MAX];
        if (mock_read_request(s, conn, req, sizeof(req)) != 0) {
            close(conn);
            continue;
        }
        pthread_mutex_lock(&s->mu);
        int idx = s->nrequests;
        if (idx < MOCK_MAX_REQUESTS) {
            memcpy(s->reqs[idx], req, sizeof(req));
            s->nrequests++;
        }
        pthread_mutex_unlock(&s->mu);

        if (idx >= s->nscript && s->hold_after_script) {
            /* Hold: answer nothing until the client closes or we stop. */
            char sink[512];
            while (!s->stop) {
                struct pollfd c = { conn, POLLIN, 0 };
                if (poll(&c, 1, 50) <= 0) continue;
                if (read(conn, sink, sizeof(sink)) <= 0) break;
            }
            close(conn);
            continue;
        }
        const mock_response_t *r =
            &s->script[idx < s->nscript ? idx : (s->nscript > 0 ? s->nscript - 1 : 0)];
        const char *body = r->body ? r->body : "";
        char hdr[256];
        int hl = snprintf(hdr, sizeof(hdr),
                          "HTTP/1.1 %d X\r\nContent-Type: application/json\r\n"
                          "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                          r->status ? r->status : 200, strlen(body));
        mock_write_all(conn, hdr, (size_t)hl);
        mock_write_all(conn, body, strlen(body));
        close(conn);
    }
    return NULL;
}

/* Start listening on an ephemeral port. Returns 0 on success. */
static int mock_server_start(mock_server_t *s)
{
    s->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s->listen_fd < 0) return -1;
    int one = 1;
    setsockopt(s->listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(s->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) return -1;
    if (listen(s->listen_fd, 16) != 0) return -1;
    socklen_t alen = sizeof(addr);
    if (getsockname(s->listen_fd, (struct sockaddr *)&addr, &alen) != 0) return -1;
    s->port = (int)ntohs(addr.sin_port);
    pthread_mutex_init(&s->mu, NULL);
    return pthread_create(&s->thread, NULL, mock_server_main, s) == 0 ? 0 : -1;
}

static void mock_server_stop(mock_server_t *s)
{
    s->stop = 1;
    pthread_join(s->thread, NULL);
    close(s->listen_fd);
    pthread_mutex_destroy(&s->mu);
}

/* Wait until the server has seen at least `n` requests or `timeout_ms`
 * passed. Returns the count. */
static int mock_wait_requests(mock_server_t *s, int n, long timeout_ms)
{
    long deadline = mock_now_ms() + timeout_ms;
    int got = mock_request_count(s);
    while (got < n && mock_now_ms() < deadline) {
        mock_sleep_ms(10);
        got = mock_request_count(s);
    }
    return got;
}

#endif /* AUDD_MOCK_HTTP_SERVER_H */
