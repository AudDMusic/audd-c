/* audd_error.c — error sentinel mapping + last-error scratch. */
#include "audd_internal.h"
#include "audd.h"

#include <string.h>

audd_error_t audd_sentinel_for_code(int code)
{
    switch (code) {
    case 900:
    case 901:
    case 903:
        return AUDD_ERR_AUTHENTICATION;
    case 902:
        return AUDD_ERR_QUOTA;
    case 904:
    case 905:
        return AUDD_ERR_SUBSCRIPTION;
    case 50:
    case 51:
    case 600:
    case 601:
    case 602:
    case 700:
    case 701:
    case 702:
    case 906:
        return AUDD_ERR_INVALID_REQUEST;
    case 300:
    case 400:
    case 500:
        return AUDD_ERR_INVALID_AUDIO;
    case 610:
        return AUDD_ERR_STREAM_LIMIT;
    case 611:
        return AUDD_ERR_RATE_LIMIT;
    case 907:
        return AUDD_ERR_NOT_RELEASED;
    case 19:
    case 31337:
        return AUDD_ERR_BLOCKED;
    case 20:
        return AUDD_ERR_NEEDS_UPDATE;
    default:
        return AUDD_ERR_SERVER;
    }
}

const char *audd_error_string(audd_error_t err)
{
    switch (err) {
    case AUDD_OK:                        return "ok";
    case AUDD_ERR_AUTHENTICATION:        return "audd: authentication error";
    case AUDD_ERR_QUOTA:                 return "audd: quota exceeded";
    case AUDD_ERR_SUBSCRIPTION:          return "audd: endpoint not enabled on token";
    case AUDD_ERR_CUSTOM_CATALOG_ACCESS: return "audd: custom catalog access denied";
    case AUDD_ERR_INVALID_REQUEST:       return "audd: invalid request";
    case AUDD_ERR_INVALID_AUDIO:         return "audd: invalid audio";
    case AUDD_ERR_RATE_LIMIT:            return "audd: rate limit reached";
    case AUDD_ERR_STREAM_LIMIT:          return "audd: stream slot limit reached";
    case AUDD_ERR_NOT_RELEASED:          return "audd: song not yet released";
    case AUDD_ERR_BLOCKED:               return "audd: blocked by audd security";
    case AUDD_ERR_NEEDS_UPDATE:          return "audd: client update required";
    case AUDD_ERR_SERVER:                return "audd: server error";
    case AUDD_ERR_CONNECTION:            return "audd: connection error";
    case AUDD_ERR_SERIALIZATION:         return "audd: serialization error";
    case AUDD_ERR_INVALID_ARGUMENT:      return "audd: invalid argument";
    case AUDD_ERR_OUT_OF_MEMORY:         return "audd: out of memory";
    case AUDD_ERR_IO:                    return "audd: I/O error";
    case AUDD_ERR_CLOSED:                return "audd: client closed";
    case AUDD_ERR_NOT_FOUND:             return "audd: not found";
    }
    return "audd: unknown error";
}

void audd_client_set_error(audd_client_t *client, const char *message, int api_code)
{
    if (client == NULL) return;
    audd_free(client->last_error_message);
    client->last_error_message = NULL;
    if (message != NULL) {
        client->last_error_message = audd_strdup(message);
    }
    client->last_error_code = api_code;
}

void audd_client_set_request_id(audd_client_t *client, const char *rid)
{
    if (client == NULL) return;
    audd_free(client->last_request_id);
    client->last_request_id = (rid != NULL) ? audd_strdup(rid) : NULL;
}

void audd_client_clear_error(audd_client_t *client)
{
    if (client == NULL) return;
    audd_free(client->last_error_message);
    client->last_error_message = NULL;
    client->last_error_code = 0;
}

const char *audd_last_error_message(const audd_client_t *client)
{
    if (client == NULL || client->last_error_message == NULL) {
        return "";
    }
    return client->last_error_message;
}

int audd_last_error_code(const audd_client_t *client)
{
    if (client == NULL) return 0;
    return client->last_error_code;
}

const char *audd_last_request_id(const audd_client_t *client)
{
    if (client == NULL || client->last_request_id == NULL) {
        return "";
    }
    return client->last_request_id;
}
