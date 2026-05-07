/* audd_json.c — small wrappers over cJSON. */
#include "audd_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../vendor/cJSON/cJSON.h"

const char *audd_json_get_string(const cJSON *obj, const char *key)
{
    if (obj == NULL) return NULL;
    cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (it == NULL || !cJSON_IsString(it)) return NULL;
    return it->valuestring;
}

int audd_json_get_int(const cJSON *obj, const char *key, int def)
{
    if (obj == NULL) return def;
    cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (it == NULL) return def;
    if (cJSON_IsNumber(it)) return (int)it->valuedouble;
    if (cJSON_IsString(it) && it->valuestring != NULL) {
        return atoi(it->valuestring);
    }
    return def;
}

int64_t audd_json_get_int64(const cJSON *obj, const char *key, int64_t def)
{
    if (obj == NULL) return def;
    cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (it == NULL) return def;
    if (cJSON_IsNumber(it)) return (int64_t)it->valuedouble;
    if (cJSON_IsString(it) && it->valuestring != NULL) {
        return (int64_t)strtoll(it->valuestring, NULL, 10);
    }
    return def;
}

int audd_json_has(const cJSON *obj, const char *key)
{
    return obj != NULL && cJSON_HasObjectItem((cJSON *)obj, key);
}

int audd_json_get_bool(const cJSON *obj, const char *key, int def)
{
    if (obj == NULL) return def;
    cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (it == NULL) return def;
    if (cJSON_IsBool(it)) return cJSON_IsTrue(it) ? 1 : 0;
    if (cJSON_IsNumber(it)) return it->valuedouble != 0.0;
    return def;
}

char *audd_json_print_field(const cJSON *obj, const char *key)
{
    if (obj == NULL) return NULL;
    cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (it == NULL) return NULL;
    char *s = cJSON_PrintUnformatted(it);
    return s; /* heap, owned by cJSON's allocator (= ours via hooks) */
}

/* ------------------------------------------------------------------ *
 * Top-level response decoding.                                        *
 *
 * Mirrors decodeOrRaise in audd-go. Sets the client's last-error
 * scratch state on any error condition.                               *
 * ------------------------------------------------------------------ */

static const int kHttpClientErrorFloor = 400;
static const int kDeprecatedParamsCode = 51;

/* "Strip" the deprecation pass-through. If the server returned status:error
 * with code 51 plus a usable result, we pretend it was a success. */
static void maybe_warn_and_strip(audd_client_t *client, cJSON *body)
{
    cJSON *err = cJSON_GetObjectItemCaseSensitive(body, "error");
    if (!cJSON_IsObject(err)) return;
    cJSON *code = cJSON_GetObjectItemCaseSensitive(err, "error_code");
    if (!cJSON_IsNumber(code) || (int)code->valuedouble != kDeprecatedParamsCode) {
        return;
    }
    cJSON *result = cJSON_GetObjectItemCaseSensitive(body, "result");
    if (result == NULL || cJSON_IsNull(result)) return;

    /* swallow the error block */
    cJSON_DeleteItemFromObjectCaseSensitive(body, "error");
    cJSON *status = cJSON_GetObjectItemCaseSensitive(body, "status");
    if (status != NULL) {
        cJSON_DeleteItemFromObjectCaseSensitive(body, "status");
    }
    cJSON_AddStringToObject(body, "status", "success");
    (void)client; /* deprecation hook not yet wired through audd-c */
}

audd_error_t audd_decode_or_raise(audd_client_t *client,
                                  audd_http_response_t *resp,
                                  int custom_catalog_context)
{
    audd_client_clear_error(client);

    if (resp->json == NULL) {
        if (resp->status >= kHttpClientErrorFloor) {
            char *msg = audd_aprintf("HTTP %ld with non-JSON response body",
                                     resp->status);
            audd_client_set_error(client, msg ? msg : "HTTP error", 0);
            audd_free(msg);
            return AUDD_ERR_SERVER;
        }
        audd_client_set_error(client, "Unparseable response", 0);
        return AUDD_ERR_SERIALIZATION;
    }

    cJSON *body = resp->json;
    maybe_warn_and_strip(client, body);

    cJSON *status = cJSON_GetObjectItemCaseSensitive(body, "status");
    const char *status_str = (cJSON_IsString(status) && status->valuestring) ? status->valuestring : "";

    if (strcmp(status_str, "success") == 0) {
        return AUDD_OK;
    }
    if (strcmp(status_str, "error") == 0) {
        cJSON *err = cJSON_GetObjectItemCaseSensitive(body, "error");
        int code = 0;
        const char *msg = "";
        if (cJSON_IsObject(err)) {
            cJSON *c = cJSON_GetObjectItemCaseSensitive(err, "error_code");
            if (cJSON_IsNumber(c)) code = (int)c->valuedouble;
            const char *m = audd_json_get_string(err, "error_message");
            if (m) msg = m;
        }
        char *full = audd_aprintf("[#%d] %s", code, msg);
        audd_client_set_error(client, full ? full : msg, code);
        audd_free(full);
        if (custom_catalog_context && (code == 904 || code == 905)) {
            char *override = audd_aprintf(
                "Adding songs to your custom catalog requires enterprise access "
                "that isn't enabled on your account.\n\n"
                "Note: the custom-catalog endpoint is for adding songs to your "
                "private fingerprint database, not for music recognition. If you "
                "intended to identify music, use audd_recognize(...) (or "
                "audd_recognize_enterprise(...) for files longer than 25 seconds) "
                "instead.\n\nTo request custom-catalog access, contact "
                "api@audd.io.\n\n[Server message: %s]",
                msg);
            audd_client_set_error(client, override ? override : msg, code);
            audd_free(override);
            return AUDD_ERR_CUSTOM_CATALOG_ACCESS;
        }
        return audd_sentinel_for_code(code);
    }
    /* Unknown status. */
    char *m = audd_aprintf("Unexpected response status: %s", status_str);
    audd_client_set_error(client, m ? m : "unexpected status", 0);
    audd_free(m);
    return AUDD_ERR_SERVER;
}

audd_error_t audd_decode_top_level(audd_client_t *client,
                                   audd_http_response_t *resp)
{
    audd_client_clear_error(client);
    if (resp->json == NULL) {
        if (resp->status >= kHttpClientErrorFloor) {
            char *msg = audd_aprintf("HTTP %ld with non-JSON response body",
                                     resp->status);
            audd_client_set_error(client, msg ? msg : "HTTP error", 0);
            audd_free(msg);
            return AUDD_ERR_SERVER;
        }
        audd_client_set_error(client, "Unparseable response", 0);
        return AUDD_ERR_SERIALIZATION;
    }
    return AUDD_OK;
}
