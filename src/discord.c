/* libdiscord - Discord bot support library on top of librest
 * Pure plumbing: no syscalls, no callbacks, no active behavior (ADR 006)
 * Emits structured discord_event_t events; caller owns all parsing and policy.
 */

#include "discord.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>  /* snprintf for JSON building */
#include <stdint.h>
#include <stdarg.h> /* va_list for build_api_path */

#include <rest.h>  /* librest public API for HTTP/2 REST and WebSocket */

#define DISCORD_DEFAULT_EVENT_QUEUE_SIZE 64
#define DISCORD_MAX_URL_LEN 512
#define DISCORD_MAX_TOKEN_LEN 512
#define DISCORD_MAX_SESSION_ID_LEN 128
#define DISCORD_MAX_JSON_BODY 4096
#define DISCORD_MAX_AUTH_HEADER 600

static const char *DISCORD_API_BASE = "https://discord.com/api/v10";
/* Gateway URL used by discord_gateway_connect (default from librest) */

/* Internal Gateway state */
typedef enum {
    GATEWAY_STATE_DISCONNECTED = 0,
    GATEWAY_STATE_CONNECTING,
    GATEWAY_STATE_CONNECTED,
    GATEWAY_STATE_IDENTIFIED,
    GATEWAY_STATE_RESUMING
} gateway_state_t;

/* Opaque context implementation */
struct discord_ctx {
    discord_role_t   role;
    discord_config_t config;
    /* Event queue (ring buffer) */
    discord_event_t *event_queue;
    size_t           queue_size;
    size_t           queue_head;
    size_t           queue_tail;
    /* Auth state */
    char             bot_token[DISCORD_MAX_TOKEN_LEN];
    char             auth_header[DISCORD_MAX_AUTH_HEADER];
    /* Gateway state */
    gateway_state_t  gateway_state;
    int              heartbeat_interval_ms;
    int              last_sequence;
    char             session_id[DISCORD_MAX_SESSION_ID_LEN];
    char             resume_gateway_url[DISCORD_MAX_URL_LEN];
    /* API base URL */
    char             api_base[DISCORD_MAX_URL_LEN];
    /* Accumulated API response body */
    char            *api_response_buf;
    size_t           api_response_len;
    size_t           api_response_cap;
    /* librest context (owns HTTP/2 transport) */
    rest_ctx_t      *rest;
};

/* Internal helpers */
static int enqueue_event(discord_ctx_t *ctx, const discord_event_t *ev);
static int build_auth_header(discord_ctx_t *ctx);
static int build_api_path(char *buf, size_t max, const char *fmt, ...);
static void accumulate_api_response(discord_ctx_t *ctx, const uint8_t *data, size_t len);
static void emit_api_response(discord_ctx_t *ctx);
static void translate_rest_event(discord_ctx_t *ctx, const rest_event_t *rev);
static void translate_ws_text(discord_ctx_t *ctx, const char *text, size_t len);

/* Lifecycle */

discord_ctx_t *discord_create(discord_role_t role) {
    discord_config_t default_config = {
        .event_queue_size = DISCORD_DEFAULT_EVENT_QUEUE_SIZE,
        .bot_token = NULL,
        .gateway_url = NULL,
        .api_base_url = NULL
    };
    return discord_create_with_config(role, &default_config);
}

discord_ctx_t *discord_create_with_config(discord_role_t role, const discord_config_t *config) {
    if (!config || config->event_queue_size == 0) return NULL;

    discord_ctx_t *ctx = (discord_ctx_t *)calloc(1, sizeof(discord_ctx_t));
    if (!ctx) return NULL;

    ctx->role = role;
    ctx->config = *config;

    /* Event queue */
    ctx->event_queue = (discord_event_t *)calloc(config->event_queue_size, sizeof(discord_event_t));
    ctx->queue_size = config->event_queue_size;
    if (!ctx->event_queue) { discord_destroy(ctx); return NULL; }

    /* Copy bot token if provided */
    if (config->bot_token) {
        size_t tlen = strlen(config->bot_token);
        if (tlen >= DISCORD_MAX_TOKEN_LEN) tlen = DISCORD_MAX_TOKEN_LEN - 1;
        memcpy(ctx->bot_token, config->bot_token, tlen);
        ctx->bot_token[tlen] = '\0';
        if (build_auth_header(ctx) != 0) { discord_destroy(ctx); return NULL; }
    }

    /* API base URL */
    if (config->api_base_url) {
        size_t blen = strlen(config->api_base_url);
        if (blen >= DISCORD_MAX_URL_LEN) blen = DISCORD_MAX_URL_LEN - 1;
        memcpy(ctx->api_base, config->api_base_url, blen);
        ctx->api_base[blen] = '\0';
    } else {
        strcpy(ctx->api_base, DISCORD_API_BASE);
    }

    /* Gateway state */
    ctx->gateway_state = GATEWAY_STATE_DISCONNECTED;
    ctx->heartbeat_interval_ms = 41250; /* default Discord heartbeat interval */
    ctx->last_sequence = -1;

    /* API response accumulator */
    ctx->api_response_cap = 8192;
    ctx->api_response_buf = (char *)malloc(ctx->api_response_cap);
    if (!ctx->api_response_buf) { discord_destroy(ctx); return NULL; }
    ctx->api_response_len = 0;

    /* Create librest context */
    rest_config_t rest_cfg = {
        .event_queue_size = (size_t)config->event_queue_size,
        .default_content_type = "application/json"
    };
    rest_role_t rest_role = (role == DISCORD_ROLE_CLIENT) ? REST_ROLE_CLIENT : REST_ROLE_SERVER;
    ctx->rest = rest_create_with_config(rest_role, &rest_cfg);
    if (!ctx->rest) { discord_destroy(ctx); return NULL; }

    return ctx;
}

void discord_destroy(discord_ctx_t *ctx) {
    if (!ctx) return;
    if (ctx->rest) {
        rest_destroy(ctx->rest);
        ctx->rest = NULL;
    }
    free(ctx->event_queue);
    free(ctx->api_response_buf);
    free(ctx);
}

/* Core I/O */

int discord_feed_input(discord_ctx_t *ctx, const uint8_t *data, size_t len) {
    if (!ctx || !data || len == 0) return -1;
    if (!ctx->rest) return -1;

    /* Feed to librest */
    int rc = rest_feed_input(ctx->rest, data, len);
    if (rc != 0) return -1;

    /* Translate REST events into Discord events */
    rest_event_t rev;
    while (rest_next_event(ctx->rest, &rev) == 1) {
        translate_rest_event(ctx, &rev);
    }

    return 0;
}

int discord_next_event(discord_ctx_t *ctx, discord_event_t *event) {
    if (!ctx || !event) return 0;
    if (ctx->queue_head == ctx->queue_tail) return 0;

    *event = ctx->event_queue[ctx->queue_head];
    ctx->queue_head = (ctx->queue_head + 1) % ctx->queue_size;
    return 1;
}

int discord_get_output(discord_ctx_t *ctx, uint8_t *buf, size_t max) {
    if (!ctx || !buf || max == 0) return 0;
    if (!ctx->rest) return 0;
    return rest_get_output(ctx->rest, buf, max);
}

/* REST API helpers */

int discord_create_message(discord_ctx_t *ctx, const char *channel_id, const char *content) {
    if (!ctx || !channel_id || !content) return -1;
    if (!ctx->rest) return -1;

    /* Build path: /channels/{channel_id}/messages */
    char path[256];
    if (build_api_path(path, sizeof(path), "/channels/%s/messages", channel_id) < 0) return -1;

    /* Build JSON body: {"content": "..."} */
    char json[DISCORD_MAX_JSON_BODY];
    int n = snprintf(json, sizeof(json), "{\"content\":\"%s\"}", content);
    if (n < 0 || (size_t)n >= sizeof(json)) return -1;

    /* Build headers with auth */
    rest_header_t headers[1];
    size_t num_headers = 0;
    if (ctx->auth_header[0]) {
        headers[0].name = "Authorization";
        headers[0].value = ctx->auth_header;
        num_headers = 1;
    }

    return rest_post_json(ctx->rest, path, json, headers, num_headers);
}

int discord_edit_message(discord_ctx_t *ctx, const char *channel_id,
                         const char *message_id, const char *content) {
    if (!ctx || !channel_id || !message_id || !content) return -1;
    if (!ctx->rest) return -1;

    /* Build path: /channels/{channel_id}/messages/{message_id} */
    char path[256];
    if (build_api_path(path, sizeof(path), "/channels/%s/messages/%s", channel_id, message_id) < 0) return -1;

    /* Build JSON body */
    char json[DISCORD_MAX_JSON_BODY];
    int n = snprintf(json, sizeof(json), "{\"content\":\"%s\"}", content);
    if (n < 0 || (size_t)n >= sizeof(json)) return -1;

    /* Build headers with auth */
    rest_header_t headers[1];
    size_t num_headers = 0;
    if (ctx->auth_header[0]) {
        headers[0].name = "Authorization";
        headers[0].value = ctx->auth_header;
        num_headers = 1;
    }

    return rest_put(ctx->rest, path, (const uint8_t *)json, strlen(json),
                    "application/json", headers, num_headers);
}

int discord_delete_message(discord_ctx_t *ctx, const char *channel_id,
                           const char *message_id) {
    if (!ctx || !channel_id || !message_id) return -1;
    if (!ctx->rest) return -1;

    /* Build path: /channels/{channel_id}/messages/{message_id} */
    char path[256];
    if (build_api_path(path, sizeof(path), "/channels/%s/messages/%s", channel_id, message_id) < 0) return -1;

    /* Build headers with auth */
    rest_header_t headers[1];
    size_t num_headers = 0;
    if (ctx->auth_header[0]) {
        headers[0].name = "Authorization";
        headers[0].value = ctx->auth_header;
        num_headers = 1;
    }

    return rest_delete(ctx->rest, path, headers, num_headers);
}

int discord_get_channel(discord_ctx_t *ctx, const char *channel_id) {
    if (!ctx || !channel_id) return -1;
    if (!ctx->rest) return -1;

    /* Build path: /channels/{channel_id} */
    char path[256];
    if (build_api_path(path, sizeof(path), "/channels/%s", channel_id) < 0) return -1;

    /* Build headers with auth */
    rest_header_t headers[1];
    size_t num_headers = 0;
    if (ctx->auth_header[0]) {
        headers[0].name = "Authorization";
        headers[0].value = ctx->auth_header;
        num_headers = 1;
    }

    return rest_get(ctx->rest, path, headers, num_headers);
}

/* Gateway helpers */

int discord_gateway_connect(discord_ctx_t *ctx) {
    if (!ctx) return -1;
    if (!ctx->rest) return -1;

    ctx->gateway_state = GATEWAY_STATE_CONNECTING;

    /* Establish binary channel (WebSocket via extended CONNECT) */
    int rc = rest_establish_binary_channel(ctx->rest);
    if (rc != 0) {
        ctx->gateway_state = GATEWAY_STATE_DISCONNECTED;
        return -1;
    }

    return 0;
}

int discord_gateway_send_identify(discord_ctx_t *ctx) {
    if (!ctx) return -1;
    if (!ctx->rest) return -1;
    if (!ctx->bot_token[0]) return -1;

    /* Build identify payload:
     * {
     *   "op": 2,
     *   "d": {
     *     "token": "...",
     *     "intents": 32767,
     *     "properties": {"os": "linux", "browser": "libdiscord", "device": "libdiscord"}
     *   }
     * }
     */
    char payload[DISCORD_MAX_JSON_BODY];
    int n = snprintf(payload, sizeof(payload),
        "{\"op\":2,\"d\":{\"token\":\"%s\","
        "\"intents\":32767,"
        "\"properties\":{\"os\":\"linux\",\"browser\":\"libdiscord\",\"device\":\"libdiscord\"}}}",
        ctx->bot_token);
    if (n < 0 || (size_t)n >= sizeof(payload)) return -1;

    return rest_ws_send_text(ctx->rest, payload);
}

int discord_gateway_send_heartbeat(discord_ctx_t *ctx, int last_sequence) {
    if (!ctx) return -1;
    if (!ctx->rest) return -1;

    /* Build heartbeat payload:
     * {"op": 1, "d": <last_sequence or null>}
     */
    char payload[128];
    int n;
    if (last_sequence >= 0) {
        n = snprintf(payload, sizeof(payload), "{\"op\":1,\"d\":%d}", last_sequence);
    } else {
        n = snprintf(payload, sizeof(payload), "{\"op\":1,\"d\":null}");
    }
    if (n < 0 || (size_t)n >= sizeof(payload)) return -1;

    return rest_ws_send_text(ctx->rest, payload);
}

int discord_gateway_send_resume(discord_ctx_t *ctx) {
    if (!ctx) return -1;
    if (!ctx->rest) return -1;
    if (!ctx->bot_token[0] || !ctx->session_id[0]) return -1;

    /* Build resume payload:
     * {"op": 6, "d": {"token": "...", "session_id": "...", "seq": <last_seq>}}
     */
    char payload[DISCORD_MAX_JSON_BODY];
    int n = snprintf(payload, sizeof(payload),
        "{\"op\":6,\"d\":{\"token\":\"%s\",\"session_id\":\"%s\",\"seq\":%d}}",
        ctx->bot_token, ctx->session_id, ctx->last_sequence);
    if (n < 0 || (size_t)n >= sizeof(payload)) return -1;

    ctx->gateway_state = GATEWAY_STATE_RESUMING;
    return rest_ws_send_text(ctx->rest, payload);
}

int discord_gateway_process_heartbeat(discord_ctx_t *ctx) {
    if (!ctx) return 0;
    /* Pure function — caller is responsible for timing.
     * This always returns 1 to indicate "send heartbeat now".
     * In a real implementation, the caller tracks the interval. */
    return 1;
}

/* Internal helpers */

static int enqueue_event(discord_ctx_t *ctx, const discord_event_t *ev) {
    if (!ctx || !ev) return -1;
    size_t next_tail = (ctx->queue_tail + 1) % ctx->queue_size;
    if (next_tail == ctx->queue_head) return -1; /* full */
    ctx->event_queue[ctx->queue_tail] = *ev;
    ctx->queue_tail = next_tail;
    return 0;
}

static int build_auth_header(discord_ctx_t *ctx) {
    if (!ctx || !ctx->bot_token[0]) return -1;
    int n = snprintf(ctx->auth_header, sizeof(ctx->auth_header), "Bot %s", ctx->bot_token);
    if (n < 0 || (size_t)n >= sizeof(ctx->auth_header)) return -1;
    return 0;
}

static int build_api_path(char *buf, size_t max, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, max, fmt, args);
    va_end(args);
    if (n < 0 || (size_t)n >= max) return -1;
    return n;
}

static void accumulate_api_response(discord_ctx_t *ctx, const uint8_t *data, size_t len) {
    if (!ctx || !data || len == 0) return;
    while (ctx->api_response_len + len > ctx->api_response_cap) {
        size_t new_cap = ctx->api_response_cap * 2;
        char *new_buf = (char *)realloc(ctx->api_response_buf, new_cap);
        if (!new_buf) return;
        ctx->api_response_buf = new_buf;
        ctx->api_response_cap = new_cap;
    }
    memcpy(ctx->api_response_buf + ctx->api_response_len, data, len);
    ctx->api_response_len += len;
}

static void emit_api_response(discord_ctx_t *ctx) {
    if (!ctx) return;
    discord_event_t ev = {0};
    ev.type = DISCORD_EVENT_API_RESPONSE;
    ev.data.api_response.json_body = ctx->api_response_buf;
    ev.data.api_response.body_len = ctx->api_response_len;
    (void)enqueue_event(ctx, &ev);
    /* Reset accumulator (pointer is still valid in event until next feed_input) */
    ctx->api_response_len = 0;
}

static void translate_rest_event(discord_ctx_t *ctx, const rest_event_t *rev) {
    if (!ctx || !rev) return;

    switch (rev->type) {
        case REST_EVENT_RESPONSE_HEADERS:
            /* API response starting — reset accumulator */
            ctx->api_response_len = 0;
            /* Extract status code if available */
            if (rev->data.status.status > 0) {
                /* Will be emitted when response completes */
            }
            break;

        case REST_EVENT_RESPONSE_DATA:
            /* Accumulate response body */
            if (rev->data.data.data && rev->data.data.len > 0) {
                accumulate_api_response(ctx, rev->data.data.data, rev->data.data.len);
            }
            break;

        case REST_EVENT_RESPONSE_COMPLETE:
            /* Emit complete API response */
            emit_api_response(ctx);
            break;

        case REST_EVENT_BINARY_CHANNEL_READY:
        {
            /* Gateway or binary channel connected */
            ctx->gateway_state = GATEWAY_STATE_CONNECTED;
            discord_event_t ev = {0};
            ev.type = DISCORD_EVENT_CONNECTED;
            (void)enqueue_event(ctx, &ev);
            break;
        }

        case REST_EVENT_WS_TEXT:
            /* Gateway message — parse and translate */
            if (rev->data.ws_message.data && rev->data.ws_message.len > 0) {
                translate_ws_text(ctx, (const char *)rev->data.ws_message.data,
                                  rev->data.ws_message.len);
            }
            break;

        case REST_EVENT_WS_BINARY:
        {
            discord_event_t ev = {0};
            ev.type = DISCORD_EVENT_RAW;
            (void)enqueue_event(ctx, &ev);
            break;
        }

        case REST_EVENT_WS_CLOSE:
        {
            ctx->gateway_state = GATEWAY_STATE_DISCONNECTED;
            discord_event_t ev = {0};
            ev.type = DISCORD_EVENT_DISCONNECTED;
            (void)enqueue_event(ctx, &ev);
            break;
        }

        case REST_EVENT_ERROR:
        {
            discord_event_t ev = {0};
            ev.type = DISCORD_EVENT_ERROR;
            ev.error_msg = rev->error_msg ? rev->error_msg : "REST error";
            (void)enqueue_event(ctx, &ev);
            break;
        }

        default:
            /* Other REST events not directly mapped to Discord events */
            break;
    }
}

/* Minimal JSON field extraction (no parser, pure string search — plumbing only).
 * Finds "key":"value" or "key":value patterns in a JSON string.
 * Returns 1 if found, 0 otherwise. Value written to val_buf. */
static int json_find_string(const char *json, size_t json_len,
                            const char *key, char *val_buf, size_t val_max) {
    if (!json || !key || !val_buf || val_max == 0) return 0;
    size_t klen = strlen(key);

    /* Search for "key": pattern */
    for (size_t i = 0; i + klen + 3 < json_len; i++) {
        if (json[i] == '"' && memcmp(json + i + 1, key, klen) == 0 &&
            json[i + 1 + klen] == '"') {
            /* Found "key", skip to colon */
            size_t pos = i + 1 + klen + 1;
            while (pos < json_len && (json[pos] == ' ' || json[pos] == ':')) pos++;
            if (pos >= json_len) return 0;

            /* Expect a string value */
            if (json[pos] == '"') {
                pos++;
                size_t start = pos;
                while (pos < json_len && json[pos] != '"') pos++;
                size_t vlen = pos - start;
                if (vlen >= val_max) vlen = val_max - 1;
                memcpy(val_buf, json + start, vlen);
                val_buf[vlen] = '\0';
                return 1;
            }
            /* Non-string value (number, null, etc.) */
            if (val_max > 0) {
                size_t start = pos;
                while (pos < json_len && json[pos] != ',' && json[pos] != '}' &&
                       json[pos] != ']' && json[pos] != ' ') pos++;
                size_t vlen = pos - start;
                if (vlen >= val_max) vlen = val_max - 1;
                memcpy(val_buf, json + start, vlen);
                val_buf[vlen] = '\0';
                return 1;
            }
        }
    }
    return 0;
}

static int json_find_int(const char *json, size_t json_len, const char *key, int *out) {
    char buf[32];
    if (!json_find_string(json, json_len, key, buf, sizeof(buf))) return 0;
    *out = atoi(buf);
    return 1;
}

/* Find "key":{...} nested object start */
static const char *json_find_object(const char *json, size_t json_len,
                                    const char *key, size_t *obj_len) {
    if (!json || !key) return NULL;
    size_t klen = strlen(key);

    for (size_t i = 0; i + klen + 3 < json_len; i++) {
        if (json[i] == '"' && memcmp(json + i + 1, key, klen) == 0 &&
            json[i + 1 + klen] == '"') {
            size_t pos = i + 1 + klen + 1;
            while (pos < json_len && (json[pos] == ' ' || json[pos] == ':')) pos++;
            if (pos >= json_len || json[pos] != '{') return NULL;
            /* Find matching brace */
            int depth = 1;
            size_t start = pos;
            pos++;
            while (pos < json_len && depth > 0) {
                if (json[pos] == '{') depth++;
                else if (json[pos] == '}') depth--;
                pos++;
            }
            if (depth == 0) {
                *obj_len = pos - start;
                return json + start;
            }
        }
    }
    return NULL;
}

static void translate_ws_text(discord_ctx_t *ctx, const char *text, size_t len) {
    if (!ctx || !text || len == 0) return;

    /* Gateway payload: {"op": <int>, "d": <data>, "s": <seq>, "t": <event_name>} */
    int op = -1;
    json_find_int(text, len, "op", &op);

    int seq = -1;
    json_find_int(text, len, "s", &seq);
    if (seq >= 0) ctx->last_sequence = seq;

    char event_name[64] = {0};
    json_find_string(text, len, "t", event_name, sizeof(event_name));

    switch (op) {
        case 0: /* Dispatch */
            if (strcmp(event_name, "READY") == 0) {
                /* Extract READY data: user.id, session_id, resume_gateway_url */
                size_t dlen = 0;
                const char *d = json_find_object(text, len, "d", &dlen);
                if (d && dlen > 0) {
                    /* Session ID */
                    json_find_string(d, dlen, "session_id",
                                     ctx->session_id, sizeof(ctx->session_id));
                    /* Resume gateway URL */
                    json_find_string(d, dlen, "resume_gateway_url",
                                     ctx->resume_gateway_url, sizeof(ctx->resume_gateway_url));
                    /* User ID — nested in "user": {"id": "..."} */
                    size_t ulen = 0;
                    const char *uobj = json_find_object(d, dlen, "user", &ulen);
                    char uid[64] = {0};
                    if (uobj && ulen > 0) {
                        json_find_string(uobj, ulen, "id", uid, sizeof(uid));
                    }

                    discord_event_t ev = {0};
                    ev.type = DISCORD_EVENT_READY;
                    ev.data.ready.user_id = ctx->session_id[0] ? uid : NULL;
                    ev.data.ready.session_id = ctx->session_id[0] ? ctx->session_id : NULL;
                    ev.data.ready.gateway_url = ctx->resume_gateway_url[0] ? ctx->resume_gateway_url : NULL;
                    ctx->gateway_state = GATEWAY_STATE_IDENTIFIED;
                    (void)enqueue_event(ctx, &ev);
                }
            } else if (strcmp(event_name, "MESSAGE_CREATE") == 0) {
                size_t dlen = 0;
                const char *d = json_find_object(text, len, "d", &dlen);
                if (d && dlen > 0) {
                    /* Store raw JSON in event (caller owns parsing) */
                    discord_event_t ev = {0};
                    ev.type = DISCORD_EVENT_MESSAGE_CREATE;
                    /* Minimal field extraction for convenience */
                    static char msg_id[64], ch_id[64], guild_id[64], author_id[64];
                    static char content[2048], timestamp[64];
                    json_find_string(d, dlen, "id", msg_id, sizeof(msg_id));
                    json_find_string(d, dlen, "channel_id", ch_id, sizeof(ch_id));
                    json_find_string(d, dlen, "guild_id", guild_id, sizeof(guild_id));
                    json_find_string(d, dlen, "content", content, sizeof(content));
                    json_find_string(d, dlen, "timestamp", timestamp, sizeof(timestamp));
                    size_t alen = 0;
                    const char *aobj = json_find_object(d, dlen, "author", &alen);
                    if (aobj && alen > 0) {
                        json_find_string(aobj, alen, "id", author_id, sizeof(author_id));
                    }
                    ev.data.message.id = msg_id;
                    ev.data.message.channel_id = ch_id;
                    ev.data.message.guild_id = guild_id[0] ? guild_id : NULL;
                    ev.data.message.author_id = author_id[0] ? author_id : NULL;
                    ev.data.message.content = content[0] ? content : NULL;
                    ev.data.message.timestamp = timestamp[0] ? timestamp : NULL;
                    (void)enqueue_event(ctx, &ev);
                }
            } else if (strcmp(event_name, "MESSAGE_UPDATE") == 0) {
                discord_event_t ev = {0};
                ev.type = DISCORD_EVENT_MESSAGE_UPDATE;
                (void)enqueue_event(ctx, &ev);
            } else if (strcmp(event_name, "MESSAGE_DELETE") == 0) {
                discord_event_t ev = {0};
                ev.type = DISCORD_EVENT_MESSAGE_DELETE;
                (void)enqueue_event(ctx, &ev);
            } else if (strcmp(event_name, "GUILD_CREATE") == 0) {
                discord_event_t ev = {0};
                ev.type = DISCORD_EVENT_GUILD_CREATE;
                (void)enqueue_event(ctx, &ev);
            } else if (strcmp(event_name, "INTERACTION_CREATE") == 0) {
                discord_event_t ev = {0};
                ev.type = DISCORD_EVENT_INTERACTION_CREATE;
                (void)enqueue_event(ctx, &ev);
            }
            break;

        case 7: /* Resume required */
            /* Caller should reconnect and send resume */
            break;

        case 9: /* Invalid session */
            ctx->session_id[0] = '\0';
            ctx->gateway_state = GATEWAY_STATE_DISCONNECTED;
            break;

        case 10: /* Hello — contains heartbeat_interval */
        {
            size_t dlen = 0;
            const char *d = json_find_object(text, len, "d", &dlen);
            if (d && dlen > 0) {
                int interval = 0;
                if (json_find_int(d, dlen, "heartbeat_interval", &interval) && interval > 0) {
                    ctx->heartbeat_interval_ms = interval;
                }
            }
            /* After Hello, send identify */
            ctx->gateway_state = GATEWAY_STATE_CONNECTED;
            break;
        }

        case 11: /* Heartbeat ACK */
            /* Nothing to emit — caller tracks acks */
            break;

        default:
            /* Unknown op — emit raw */
            break;
    }
}
