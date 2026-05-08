/* audd_custom_catalog.c — add a song to the private fingerprint database. */
#include "audd_internal.h"
#include "audd.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    int audio_id;
    const char *file_path;
    const void *bytes;
    size_t bytes_size;
} cc_ctx_t;

static int cc_attempt(audd_client_t *client,
                       audd_http_response_t *resp,
                       int *body_was_uploaded,
                       void *ud)
{
    cc_ctx_t *ctx = (cc_ctx_t *)ud;
    char id_buf[16];
    snprintf(id_buf, sizeof(id_buf), "%d", ctx->audio_id);
    const char *fields[] = { "audio_id", id_buf, NULL };
    audd_http_form_t form = {0};
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
    return audd_http_post(client, "https://api.audd.io/upload/", &form,
                           client->options.standard_timeout_seconds,
                           resp, body_was_uploaded);
}

static int file_exists(const char *path)
{
    struct stat st;
    return path != NULL && stat(path, &st) == 0 && !S_ISDIR(st.st_mode);
}

audd_error_t audd_custom_catalog_add(audd_client_t *client, int audio_id, const char *source)
{
    if (client == NULL || source == NULL) return AUDD_ERR_INVALID_ARGUMENT;
    if (!file_exists(source)) {
        audd_client_set_error(client, "audd: source must be an existing file path", 0);
        return AUDD_ERR_INVALID_ARGUMENT;
    }
    cc_ctx_t ctx = { audio_id, source, NULL, 0 };
    audd_http_response_t resp = {0};
    /* Custom-catalog upload is metered: retrying on transport failure could
     * double-charge for the same audio fingerprinting. Use a 1-attempt policy
     * — return AUDD_ERR_* immediately rather than silently re-uploading. */
    int rc = audd_retry_do(client, AUDD_RETRY_NONE, cc_attempt, &ctx, &resp);
    if (rc != 0) { audd_http_response_free(&resp); return AUDD_ERR_CONNECTION; }
    audd_error_t e = audd_decode_or_raise(client, &resp, /*custom_catalog_context=*/1);
    audd_http_response_free(&resp);
    return e;
}

audd_error_t audd_custom_catalog_add_bytes(audd_client_t *client, int audio_id,
                                            const void *data, size_t size)
{
    if (client == NULL || data == NULL || size == 0) return AUDD_ERR_INVALID_ARGUMENT;
    cc_ctx_t ctx = { audio_id, NULL, data, size };
    audd_http_response_t resp = {0};
    /* Custom-catalog upload is metered: retrying on transport failure could
     * double-charge for the same audio fingerprinting. Use a 1-attempt policy
     * — return AUDD_ERR_* immediately rather than silently re-uploading. */
    int rc = audd_retry_do(client, AUDD_RETRY_NONE, cc_attempt, &ctx, &resp);
    if (rc != 0) { audd_http_response_free(&resp); return AUDD_ERR_CONNECTION; }
    audd_error_t e = audd_decode_or_raise(client, &resp, /*custom_catalog_context=*/1);
    audd_http_response_free(&resp);
    return e;
}
