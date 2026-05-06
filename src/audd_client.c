/* audd_client.c — client lifecycle, options, last-error accessors. */
#include "audd_internal.h"
#include "audd.h"
#include "../include/audd_version.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *kEnvToken = "AUDD_API_TOKEN";

audd_options_t audd_options_default(void)
{
    audd_options_t o;
    o.standard_timeout_seconds = 60;
    o.enterprise_timeout_seconds = 7200;
    o.max_attempts = 3;
    o.backoff_ms = 500;
    o.user_agent_suffix = NULL;
    o.ca_bundle_path = NULL;
    return o;
}

static char *make_user_agent(const char *suffix)
{
    if (suffix == NULL || suffix[0] == '\0') {
        return audd_aprintf("audd-c/%s", AUDD_VERSION);
    }
    return audd_aprintf("audd-c/%s %s", AUDD_VERSION, suffix);
}

static void global_init_once(void)
{
    static int inited = 0;
    if (!inited) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        inited = 1;
    }
}

audd_client_t *audd_client_new(const char *api_token, const audd_options_t *options)
{
    global_init_once();
    audd_client_t *c = (audd_client_t *)audd_malloc(sizeof(*c));
    if (c == NULL) return NULL;
    memset(c, 0, sizeof(*c));
    c->options = options ? *options : audd_options_default();
    if (c->options.standard_timeout_seconds <= 0) c->options.standard_timeout_seconds = 60;
    if (c->options.enterprise_timeout_seconds <= 0) c->options.enterprise_timeout_seconds = 7200;
    if (c->options.max_attempts <= 0) c->options.max_attempts = 3;
    if (c->options.backoff_ms <= 0) c->options.backoff_ms = 500;

    const char *token = api_token;
    if (token == NULL || token[0] == '\0') {
        token = getenv(kEnvToken);
    }
    if (token != NULL && token[0] != '\0') {
        c->api_token = audd_strdup(token);
        if (c->api_token == NULL) {
            audd_client_free(c);
            return NULL;
        }
    }

    c->user_agent = make_user_agent(c->options.user_agent_suffix);
    if (c->user_agent == NULL) {
        audd_client_free(c);
        return NULL;
    }
    if (c->options.ca_bundle_path != NULL) {
        c->ca_bundle_path = audd_strdup(c->options.ca_bundle_path);
        if (c->ca_bundle_path == NULL) {
            audd_client_free(c);
            return NULL;
        }
    }
    return c;
}

audd_client_t *audd_client_new_strict(const char *api_token,
                                       const audd_options_t *options,
                                       audd_error_t *err)
{
    const char *token = api_token;
    if (token == NULL || token[0] == '\0') {
        token = getenv(kEnvToken);
    }
    if (token == NULL || token[0] == '\0') {
        if (err) *err = AUDD_ERR_AUTHENTICATION;
        return NULL;
    }
    audd_client_t *c = audd_client_new(api_token, options);
    if (c == NULL) {
        if (err) *err = AUDD_ERR_OUT_OF_MEMORY;
        return NULL;
    }
    if (err) *err = AUDD_OK;
    return c;
}

void audd_client_free(audd_client_t *client)
{
    if (client == NULL) return;
    client->closed = 1;
    audd_free(client->api_token);
    audd_free(client->user_agent);
    audd_free(client->ca_bundle_path);
    audd_free(client->last_error_message);
    audd_free(client->last_request_id);
    audd_free(client);
}

audd_error_t audd_client_set_api_token(audd_client_t *client, const char *new_token)
{
    if (client == NULL || new_token == NULL || new_token[0] == '\0') {
        return AUDD_ERR_INVALID_ARGUMENT;
    }
    char *dup = audd_strdup(new_token);
    if (dup == NULL) return AUDD_ERR_OUT_OF_MEMORY;
    audd_free(client->api_token);
    client->api_token = dup;
    return AUDD_OK;
}

const char *audd_client_api_token(const audd_client_t *client)
{
    if (client == NULL) return NULL;
    return client->api_token;
}

void audd_string_free(char *s)
{
    audd_free(s);
}
