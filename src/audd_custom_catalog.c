/* audd_custom_catalog.c — add a song to the private fingerprint database. */
#include "audd_internal.h"
#include "audd.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    int audio_id;
    const char *url;        /* http(s) source, sent as the `url` field */
    const char *file_path;  /* local file, uploaded as the `file` part */
    const void *bytes;
    size_t bytes_size;
} cc_ctx_t;

static int cc_attempt(audd_client_t *client,
                       audd_http_response_t *resp,
                       int *body_was_uploaded,
                       void *ud)
{
    cc_ctx_t *ctx = (cc_ctx_t *)ud;
    char *url = audd_aprintf("%s/upload/", audd_client_api_base(client));
    if (url == NULL) {
        audd_client_set_error(client, "audd: out of memory", 0);
        return -1;
    }
    int rc;
    char id_buf[16];
    snprintf(id_buf, sizeof(id_buf), "%d", ctx->audio_id);
    audd_http_form_t form = {0};
    if (ctx->url != NULL) {
        const char *fields[] = { "audio_id", id_buf, "url", ctx->url, NULL };
        form.fields = fields;
        rc = audd_http_post(client, url, &form, client->options.standard_timeout_seconds,
                            resp, body_was_uploaded);
        audd_free(url);
        return rc;
    }
    const char *fields[] = { "audio_id", id_buf, NULL };
    form.fields = fields;
    audd_http_file_t file = {0};
    file.name = "file";
    file.content_type = "application/octet-stream";
    if (ctx->file_path) {
        const char *base = strrchr(ctx->file_path, '/');
        file.filename = base ? base + 1 : ctx->file_path;
        file.path = ctx->file_path;
    } else {
        file.filename = "upload.bin";
        file.data = ctx->bytes;
        file.size = ctx->bytes_size;
    }
    form.file = &file;
    rc = audd_http_post(client, url, &form, client->options.standard_timeout_seconds,
                        resp, body_was_uploaded);
    audd_free(url);
    return rc;
}

static int file_exists(const char *path)
{
    struct stat st;
    return path != NULL && stat(path, &st) == 0 && !S_ISDIR(st.st_mode);
}

/* One attempt only: an upload is never retried automatically, so the caller
 * checks the error before sending it again (an upload to an audio_id that
 * already holds a song replaces it). */
static audd_error_t cc_send(audd_client_t *client, cc_ctx_t *ctx)
{
    audd_http_response_t resp = {0};
    int rc = audd_retry_do(client, AUDD_RETRY_NONE, cc_attempt, ctx, &resp);
    if (rc != 0) { audd_http_response_free(&resp); return audd_transport_error(client); }
    audd_error_t e = audd_decode_or_raise(client, &resp, /*custom_catalog_context=*/1);
    audd_http_response_free(&resp);
    return e;
}

audd_error_t audd_custom_catalog_add(audd_client_t *client, int audio_id, const char *source)
{
    if (client == NULL || source == NULL) return AUDD_ERR_INVALID_ARGUMENT;
    cc_ctx_t ctx = { audio_id, NULL, NULL, NULL, 0 };
    if (audd_url_is_http(source)) {
        ctx.url = source;
    } else if (file_exists(source)) {
        ctx.file_path = source;
    } else {
        audd_client_set_error(client,
            "audd: source must be an HTTP URL (http:// or https://) or an existing file path", 0);
        return AUDD_ERR_INVALID_ARGUMENT;
    }
    return cc_send(client, &ctx);
}

audd_error_t audd_custom_catalog_add_bytes(audd_client_t *client, int audio_id,
                                            const void *data, size_t size)
{
    if (client == NULL || data == NULL || size == 0) return AUDD_ERR_INVALID_ARGUMENT;
    cc_ctx_t ctx = { audio_id, NULL, NULL, data, size };
    return cc_send(client, &ctx);
}
