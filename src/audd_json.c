/* audd_json.c — small wrappers over cJSON.
 *
 * Lenient parsing with scalar coercion. Well-typed fields take a fast path.
 * When the JSON type doesn't match the expected type but is convertible, the
 * value is coerced (mirrors the family ForwardCompat policy): numbers/bools
 * render to strings, numeric strings and floats parse to ints, and so on.
 * Non-convertible values (junk strings, wrong-shaped containers) degrade to
 * the caller's default — never a garbage partial parse.
 */
#include "audd_internal.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../vendor/cJSON/cJSON.h"

/* ------------------------------------------------------------------ *
 * Coercion helpers                                                    *
 * ------------------------------------------------------------------ */

/* Render a JSON number to its shortest natural decimal form: integers get no
 * decimal point ("85"), doubles render naturally ("8.5"). Writes into buf. */
static void render_number(double v, char *buf, size_t buflen)
{
    if (v == (double)(long long)v && fabs(v) < 1e18) {
        snprintf(buf, buflen, "%lld", (long long)v);
    } else {
        snprintf(buf, buflen, "%.17g", v);
        /* %.17g guarantees round-trip; trim to the shortest form that still
         * round-trips so 8.5 prints as "8.5" rather than "8.5000000000000004". */
        for (int prec = 1; prec < 17; ++prec) {
            char shorter[64];
            snprintf(shorter, sizeof(shorter), "%.*g", prec, v);
            if (strtod(shorter, NULL) == v) {
                snprintf(buf, buflen, "%s", shorter);
                break;
            }
        }
    }
}

/* Trim leading/trailing ASCII whitespace; copy the trimmed span into out.
 * Returns 0 if the trimmed span is empty or doesn't fit, 1 otherwise. */
static int trim_copy(const char *s, char *out, size_t outlen)
{
    if (s == NULL) return 0;
    while (*s != '\0' && isspace((unsigned char)*s)) s++;
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) n--;
    if (n == 0 || n >= outlen) return 0;
    memcpy(out, s, n);
    out[n] = '\0';
    return 1;
}

/* Reject hex prefixes and non-decimal forms that strtod/strtoll accept but the
 * policy forbids (0x1A, inf, nan). Assumes s is already whitespace-trimmed. */
static int looks_like_plain_decimal(const char *s)
{
    const char *p = s;
    if (*p == '+' || *p == '-') p++;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) return 0; /* hex */
    for (const char *q = p; *q != '\0'; ++q) {
        if (isalpha((unsigned char)*q)) return 0; /* inf, nan, 0x, junk */
    }
    return 1;
}

/* Full-string strict parse of a numeric string into an int64. Returns 1 on
 * success, 0 if not fully convertible. Accepts "85"; "8.9" truncates to 8. */
static int parse_int_string(const char *s, int64_t *out)
{
    char trimmed[64];
    if (!trim_copy(s, trimmed, sizeof(trimmed))) return 0;
    if (!looks_like_plain_decimal(trimmed)) return 0;

    char *end = NULL;
    errno = 0;
    long long ll = strtoll(trimmed, &end, 10);
    if (end != trimmed && *end == '\0') {
        *out = (int64_t)ll;
        return 1;
    }
    /* Fall back to strtod for fractional strings like "8.9", then truncate. */
    end = NULL;
    double d = strtod(trimmed, &end);
    if (end != trimmed && *end == '\0' && isfinite(d)) {
        *out = (int64_t)d; /* truncate toward zero */
        return 1;
    }
    return 0;
}

/* Full-string strict parse of a numeric string into a double. */
static int parse_double_string(const char *s, double *out)
{
    char trimmed[64];
    if (!trim_copy(s, trimmed, sizeof(trimmed))) return 0;
    if (!looks_like_plain_decimal(trimmed)) return 0;

    char *end = NULL;
    double d = strtod(trimmed, &end);
    if (end != trimmed && *end == '\0' && isfinite(d)) {
        *out = d;
        return 1;
    }
    return 0;
}

/* String → bool via a strict whitelist (both directions), case-insensitive and
 * whitespace-trimmed. Returns 1/0 on a recognized token, or def otherwise. */
static int parse_bool_string(const char *s, int def)
{
    char trimmed[16];
    if (s == NULL) return def;
    /* Empty (after trim) is a recognized falsey token, so handle it directly. */
    const char *p = s;
    while (*p != '\0' && isspace((unsigned char)*p)) p++;
    if (*p == '\0') return 0;
    if (!trim_copy(s, trimmed, sizeof(trimmed))) return def; /* too long: not a token */
    for (char *q = trimmed; *q != '\0'; ++q) *q = (char)tolower((unsigned char)*q);

    if (strcmp(trimmed, "true") == 0 || strcmp(trimmed, "1") == 0 ||
        strcmp(trimmed, "yes") == 0 || strcmp(trimmed, "on") == 0) {
        return 1;
    }
    if (strcmp(trimmed, "false") == 0 || strcmp(trimmed, "0") == 0 ||
        strcmp(trimmed, "no") == 0 || strcmp(trimmed, "off") == 0) {
        return 0;
    }
    return def;
}

/* Coerce a number/bool node into a string node IN PLACE, so the borrowed
 * valuestring pointer stays valid until cJSON_Delete and callers keep their
 * usual strdup. The rendered string is allocated with the SDK allocator (same
 * hooks cJSON_Delete frees through), so ownership/free semantics are identical
 * to a natively-typed string field. Returns the borrowed valuestring or NULL. */
static const char *coerce_node_to_string(cJSON *it)
{
    char buf[64];
    if (cJSON_IsNumber(it)) {
        render_number(it->valuedouble, buf, sizeof(buf));
    } else if (cJSON_IsBool(it)) {
        snprintf(buf, sizeof(buf), "%s", cJSON_IsTrue(it) ? "true" : "false");
    } else {
        return NULL;
    }
    char *copy = audd_strdup(buf);
    if (copy == NULL) return NULL;
    if (it->valuestring != NULL) audd_free(it->valuestring);
    it->valuestring = copy;
    it->type = cJSON_String;
    return it->valuestring;
}

const char *audd_json_get_string(const cJSON *obj, const char *key)
{
    if (obj == NULL) return NULL;
    cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (it == NULL) return NULL;
    if (cJSON_IsString(it)) return it->valuestring; /* fast path */
    if (cJSON_IsNumber(it) || cJSON_IsBool(it)) {
        return coerce_node_to_string(it); /* object/array/null -> NULL */
    }
    return NULL;
}

int audd_json_get_int(const cJSON *obj, const char *key, int def)
{
    return (int)audd_json_get_int64(obj, key, def);
}

int64_t audd_json_get_int64(const cJSON *obj, const char *key, int64_t def)
{
    if (obj == NULL) return def;
    cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (it == NULL) return def;
    if (cJSON_IsNumber(it)) return (int64_t)it->valuedouble; /* truncate toward 0 */
    if (cJSON_IsBool(it)) return cJSON_IsTrue(it) ? 1 : 0;
    if (cJSON_IsString(it)) {
        int64_t v = 0;
        if (parse_int_string(it->valuestring, &v)) return v;
    }
    return def;
}

double audd_json_get_double(const cJSON *obj, const char *key, double def)
{
    if (obj == NULL) return def;
    cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (it == NULL) return def;
    if (cJSON_IsNumber(it)) return it->valuedouble;
    if (cJSON_IsBool(it)) return cJSON_IsTrue(it) ? 1.0 : 0.0;
    if (cJSON_IsString(it)) {
        double v = 0.0;
        if (parse_double_string(it->valuestring, &v)) return v;
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
    if (cJSON_IsString(it)) return parse_bool_string(it->valuestring, def);
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
                "audd_recognize_enterprise(...) to scan beyond the first 12 seconds "
                "of a file) "
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
