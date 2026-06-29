/* libdiscord - Discord bot support library on top of librest
 * Pure plumbing: no syscalls, no callbacks, no active behavior (ADR 006)
 * Emits structured discord_event_t events; caller owns all parsing and policy.
 */

#include "discord.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp for case-insensitive header comparison */
#include <stdio.h>  /* snprintf for JSON building */
#include <stdint.h>
#include <stdarg.h> /* va_list for build_api_path */

#include <rest.h>  /* librest public API for HTTP/2 REST and WebSocket */

#define DISCORD_DEFAULT_EVENT_QUEUE_SIZE 64
#define DISCORD_MAX_TOKEN_LEN 512
#define DISCORD_MAX_JSON_BODY 4096
#define DISCORD_MAX_AUTH_HEADER 600
#define DISCORD_MAX_PATH 256

static const char *DISCORD_API_BASE = "https://discord.com/api/v10";

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
    char             session_id[DISCORD_SESSION_ID_LEN];
    char             resume_gateway_url[DISCORD_URL_LEN];
    uint64_t         last_heartbeat_ms;  /* monotonic tick of last heartbeat send */
    /* API base URL */
    char             api_base[DISCORD_URL_LEN];
    /* Accumulated API response body */
    char            *api_response_buf;
    size_t           api_response_len;
    size_t           api_response_cap;
    int              pending_status_code; /* status from RESPONSE_HEADERS */
    /* Rate limit tracking (accumulated from REST_EVENT_HEADER_PROVIDED) */
    int              pending_retry_after_ms; /* Retry-After * 1000 */
    int              pending_ratelimit_remaining; /* X-RateLimit-Remaining */
    int              pending_ratelimit_reset_after_ms; /* X-RateLimit-Reset-After * 1000 */
    int              pending_ratelimit_global; /* 1 if global rate limit */
    char             pending_ratelimit_bucket[DISCORD_ID_LEN]; /* X-RateLimit-Bucket */
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
static int json_find_string(const char *json, size_t json_len,
                            const char *key, char *val_buf, size_t val_max);
static int json_find_int(const char *json, size_t json_len, const char *key, int *out);
static const char *json_find_object(const char *json, size_t json_len,
                                    const char *key, size_t *obj_len);
static int rest_helper_with_auth(discord_ctx_t *ctx, rest_header_t *headers,
                                 size_t *num_headers);

/* ═══════════════════════════════════════════════════════════════════════
 * Lifecycle
 * ═══════════════════════════════════════════════════════════════════════ */

discord_ctx_t *discord_create(discord_role_t role) {
    discord_config_t default_config = {
        .event_queue_size = DISCORD_DEFAULT_EVENT_QUEUE_SIZE,
        .bot_token = NULL,
        .gateway_url = NULL,
        .api_base_url = NULL,
        .intents = 0,
        .shard_id = 0,
        .num_shards = 0
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
        if (blen >= DISCORD_URL_LEN) blen = DISCORD_URL_LEN - 1;
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
    ctx->pending_status_code = 0;

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

void discord_reset(discord_ctx_t *ctx) {
    if (!ctx) return;
    /* Clear event queue */
    ctx->queue_head = 0;
    ctx->queue_tail = 0;
    /* Reset gateway state */
    ctx->gateway_state = GATEWAY_STATE_DISCONNECTED;
    ctx->session_id[0] = '\0';
    ctx->resume_gateway_url[0] = '\0';
    ctx->last_sequence = -1;
    ctx->last_heartbeat_ms = 0;
    ctx->heartbeat_interval_ms = 41250;
    /* Clear API response accumulator */
    ctx->api_response_len = 0;
    ctx->pending_status_code = 0;
    /* Clear rate limit tracking */
    ctx->pending_retry_after_ms = 0;
    ctx->pending_ratelimit_remaining = -1;
    ctx->pending_ratelimit_reset_after_ms = 0;
    ctx->pending_ratelimit_global = 0;
    ctx->pending_ratelimit_bucket[0] = '\0';
    /* Reset librest context if present */
    if (ctx->rest) {
        rest_destroy(ctx->rest);
        rest_config_t rest_cfg = {
            .event_queue_size = ctx->config.event_queue_size,
            .default_content_type = "application/json"
        };
        rest_role_t rest_role = (ctx->role == DISCORD_ROLE_CLIENT) ? REST_ROLE_CLIENT : REST_ROLE_SERVER;
        ctx->rest = rest_create_with_config(rest_role, &rest_cfg);
    }
}

int discord_set_token(discord_ctx_t *ctx, const char *token) {
    if (!ctx || !token) return -1;
    size_t tlen = strlen(token);
    if (tlen >= DISCORD_MAX_TOKEN_LEN) tlen = DISCORD_MAX_TOKEN_LEN - 1;
    memcpy(ctx->bot_token, token, tlen);
    ctx->bot_token[tlen] = '\0';
    return build_auth_header(ctx);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Core I/O
 * ═══════════════════════════════════════════════════════════════════════ */

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

/* ═══════════════════════════════════════════════════════════════════════
 * Gateway state accessor
 * ═══════════════════════════════════════════════════════════════════════ */

discord_gateway_state_t discord_gateway_state(discord_ctx_t *ctx) {
    if (!ctx) return DISCORD_GW_DISCONNECTED;
    switch (ctx->gateway_state) {
        case GATEWAY_STATE_CONNECTING:  return DISCORD_GW_CONNECTING;
        case GATEWAY_STATE_CONNECTED:   return DISCORD_GW_CONNECTED;
        case GATEWAY_STATE_IDENTIFIED:  return DISCORD_GW_IDENTIFIED;
        case GATEWAY_STATE_RESUMING:    return DISCORD_GW_RESUMING;
        default:                        return DISCORD_GW_DISCONNECTED;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * REST API helpers
 * ═══════════════════════════════════════════════════════════════════════ */

/* Helper: populate auth header if available. Returns count (0 or 1). */
static int rest_helper_with_auth(discord_ctx_t *ctx, rest_header_t *headers,
                                 size_t *num_headers) {
    *num_headers = 0;
    if (ctx->auth_header[0]) {
        headers[0].name = "Authorization";
        headers[0].value = ctx->auth_header;
        *num_headers = 1;
    }
    return 0;
}

int discord_create_message(discord_ctx_t *ctx, const char *channel_id, const char *content) {
    return discord_create_message_ex(ctx, channel_id, content, NULL, NULL);
}

int discord_create_message_ex(discord_ctx_t *ctx, const char *channel_id,
                              const char *content, const char *embeds_json,
                              const char *components_json) {
    if (!ctx || !channel_id) return -1;
    if (!ctx->rest) return -1;

    char path[DISCORD_MAX_PATH];
    if (build_api_path(path, sizeof(path), "/channels/%s/messages", channel_id) < 0) return -1;

    /* Build JSON body */
    char json[DISCORD_MAX_JSON_BODY];
    int n;

    if (embeds_json && components_json) {
        n = snprintf(json, sizeof(json),
            "{\"content\":\"%s\",\"embeds\":%s,\"components\":%s}",
            content ? content : "", embeds_json, components_json);
    } else if (embeds_json) {
        n = snprintf(json, sizeof(json),
            "{\"content\":\"%s\",\"embeds\":%s}",
            content ? content : "", embeds_json);
    } else if (components_json) {
        n = snprintf(json, sizeof(json),
            "{\"content\":\"%s\",\"components\":%s}",
            content ? content : "", components_json);
    } else {
        n = snprintf(json, sizeof(json), "{\"content\":\"%s\"}", content ? content : "");
    }
    if (n < 0 || (size_t)n >= sizeof(json)) return -1;

    rest_header_t headers[1];
    size_t num_headers;
    rest_helper_with_auth(ctx, headers, &num_headers);

    return rest_post_json(ctx->rest, path, json, headers, num_headers);
}

int discord_edit_message(discord_ctx_t *ctx, const char *channel_id,
                         const char *message_id, const char *content) {
    if (!ctx || !channel_id || !message_id || !content) return -1;
    if (!ctx->rest) return -1;

    char path[DISCORD_MAX_PATH];
    if (build_api_path(path, sizeof(path), "/channels/%s/messages/%s",
                       channel_id, message_id) < 0) return -1;

    char json[DISCORD_MAX_JSON_BODY];
    int n = snprintf(json, sizeof(json), "{\"content\":\"%s\"}", content);
    if (n < 0 || (size_t)n >= sizeof(json)) return -1;

    rest_header_t headers[1];
    size_t num_headers;
    rest_helper_with_auth(ctx, headers, &num_headers);

    return rest_put(ctx->rest, path, (const uint8_t *)json, strlen(json),
                    "application/json", headers, num_headers);
}

int discord_delete_message(discord_ctx_t *ctx, const char *channel_id,
                           const char *message_id) {
    if (!ctx || !channel_id || !message_id) return -1;
    if (!ctx->rest) return -1;

    char path[DISCORD_MAX_PATH];
    if (build_api_path(path, sizeof(path), "/channels/%s/messages/%s",
                       channel_id, message_id) < 0) return -1;

    rest_header_t headers[1];
    size_t num_headers;
    rest_helper_with_auth(ctx, headers, &num_headers);

    return rest_delete(ctx->rest, path, headers, num_headers);
}

int discord_get_channel(discord_ctx_t *ctx, const char *channel_id) {
    if (!ctx || !channel_id) return -1;
    if (!ctx->rest) return -1;

    char path[DISCORD_MAX_PATH];
    if (build_api_path(path, sizeof(path), "/channels/%s", channel_id) < 0) return -1;

    rest_header_t headers[1];
    size_t num_headers;
    rest_helper_with_auth(ctx, headers, &num_headers);

    return rest_get(ctx->rest, path, headers, num_headers);
}

int discord_get_guild(discord_ctx_t *ctx, const char *guild_id) {
    if (!ctx || !guild_id) return -1;
    if (!ctx->rest) return -1;

    char path[DISCORD_MAX_PATH];
    if (build_api_path(path, sizeof(path), "/guilds/%s", guild_id) < 0) return -1;

    rest_header_t headers[1];
    size_t num_headers;
    rest_helper_with_auth(ctx, headers, &num_headers);

    return rest_get(ctx->rest, path, headers, num_headers);
}

int discord_get_channel_messages(discord_ctx_t *ctx, const char *channel_id) {
    if (!ctx || !channel_id) return -1;
    if (!ctx->rest) return -1;

    char path[DISCORD_MAX_PATH];
    if (build_api_path(path, sizeof(path), "/channels/%s/messages", channel_id) < 0)
        return -1;

    rest_header_t headers[1];
    size_t num_headers;
    rest_helper_with_auth(ctx, headers, &num_headers);

    return rest_get(ctx->rest, path, headers, num_headers);
}

int discord_add_reaction(discord_ctx_t *ctx, const char *channel_id,
                         const char *message_id, const char *emoji) {
    if (!ctx || !channel_id || !message_id || !emoji) return -1;
    if (!ctx->rest) return -1;

    char path[DISCORD_MAX_PATH];
    if (build_api_path(path, sizeof(path), "/channels/%s/messages/%s/reactions/%s/@me",
                       channel_id, message_id, emoji) < 0) return -1;

    rest_header_t headers[1];
    size_t num_headers;
    rest_helper_with_auth(ctx, headers, &num_headers);

    return rest_put(ctx->rest, path, NULL, 0, "application/json", headers, num_headers);
}

int discord_delete_own_reaction(discord_ctx_t *ctx, const char *channel_id,
                                const char *message_id, const char *emoji) {
    if (!ctx || !channel_id || !message_id || !emoji) return -1;
    if (!ctx->rest) return -1;

    char path[DISCORD_MAX_PATH];
    if (build_api_path(path, sizeof(path), "/channels/%s/messages/%s/reactions/%s/@me",
                       channel_id, message_id, emoji) < 0) return -1;

    rest_header_t headers[1];
    size_t num_headers;
    rest_helper_with_auth(ctx, headers, &num_headers);

    return rest_delete(ctx->rest, path, headers, num_headers);
}

int discord_create_interaction_response(discord_ctx_t *ctx,
                                        const char *interaction_id,
                                        const char *interaction_token,
                                        const char *json) {
    if (!ctx || !interaction_id || !interaction_token || !json) return -1;
    if (!ctx->rest) return -1;

    char path[DISCORD_MAX_PATH];
    if (build_api_path(path, sizeof(path), "/interactions/%s/%s/callback",
                       interaction_id, interaction_token) < 0) return -1;

    rest_header_t headers[1];
    size_t num_headers;
    rest_helper_with_auth(ctx, headers, &num_headers);

    return rest_post_json(ctx->rest, path, json, headers, num_headers);
}

int discord_create_dm(discord_ctx_t *ctx, const char *recipient_id) {
    if (!ctx || !recipient_id) return -1;
    if (!ctx->rest) return -1;

    char json[DISCORD_MAX_JSON_BODY];
    int n = snprintf(json, sizeof(json), "{\"recipient_id\":\"%s\"}", recipient_id);
    if (n < 0 || (size_t)n >= sizeof(json)) return -1;

    rest_header_t headers[1];
    size_t num_headers;
    rest_helper_with_auth(ctx, headers, &num_headers);

    return rest_post_json(ctx->rest, "/users/@me/channels", json, headers, num_headers);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Gateway helpers
 * ═══════════════════════════════════════════════════════════════════════ */

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

    /* Resolve intents */
    int intents = ctx->config.intents;
    if (intents == 0) intents = DISCORD_INTENTS_DEFAULT;

    /* Build identify payload */
    char payload[DISCORD_MAX_JSON_BODY];
    int n;

    if (ctx->config.num_shards > 0) {
        n = snprintf(payload, sizeof(payload),
            "{\"op\":2,\"d\":{\"token\":\"%s\","
            "\"intents\":%d,"
            "\"shard\":[%d,%d],"
            "\"properties\":{\"os\":\"linux\",\"browser\":\"libdiscord\",\"device\":\"libdiscord\"}}}",
            ctx->bot_token, intents, ctx->config.shard_id, ctx->config.num_shards);
    } else {
        n = snprintf(payload, sizeof(payload),
            "{\"op\":2,\"d\":{\"token\":\"%s\","
            "\"intents\":%d,"
            "\"properties\":{\"os\":\"linux\",\"browser\":\"libdiscord\",\"device\":\"libdiscord\"}}}",
            ctx->bot_token, intents);
    }
    if (n < 0 || (size_t)n >= sizeof(payload)) return -1;

    return rest_ws_send_text(ctx->rest, payload);
}

int discord_gateway_send_heartbeat(discord_ctx_t *ctx, int last_sequence, uint64_t now_ms) {
    if (!ctx) return -1;
    if (!ctx->rest) return -1;

    char payload[128];
    int n;
    if (last_sequence >= 0) {
        n = snprintf(payload, sizeof(payload), "{\"op\":1,\"d\":%d}", last_sequence);
    } else {
        n = snprintf(payload, sizeof(payload), "{\"op\":1,\"d\":null}");
    }
    if (n < 0 || (size_t)n >= sizeof(payload)) return -1;

    ctx->last_heartbeat_ms = now_ms;
    return rest_ws_send_text(ctx->rest, payload);
}

int discord_gateway_send_resume(discord_ctx_t *ctx) {
    if (!ctx) return -1;
    if (!ctx->rest) return -1;
    if (!ctx->bot_token[0] || !ctx->session_id[0]) return -1;

    char payload[DISCORD_MAX_JSON_BODY];
    int n = snprintf(payload, sizeof(payload),
        "{\"op\":6,\"d\":{\"token\":\"%s\",\"session_id\":\"%s\",\"seq\":%d}}",
        ctx->bot_token, ctx->session_id, ctx->last_sequence);
    if (n < 0 || (size_t)n >= sizeof(payload)) return -1;

    ctx->gateway_state = GATEWAY_STATE_RESUMING;
    return rest_ws_send_text(ctx->rest, payload);
}

int discord_gateway_process_heartbeat(discord_ctx_t *ctx, uint64_t now_ms) {
    if (!ctx) return 0;
    /* First heartbeat: send immediately after Hello */
    if (ctx->last_heartbeat_ms == 0) return 1;
    /* Check if interval has elapsed */
    if (now_ms - ctx->last_heartbeat_ms >= (uint64_t)ctx->heartbeat_interval_ms) {
        return 1;
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Internal helpers
 * ═══════════════════════════════════════════════════════════════════════ */

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
    discord_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = DISCORD_EVENT_API_RESPONSE;
    ev.data.api_response.status_code = ctx->pending_status_code;
    /* Copy body into event's embedded buffer (truncated if needed) */
    size_t copy_len = ctx->api_response_len;
    int truncated = 0;
    if (copy_len >= DISCORD_EV_BODY_CAP) {
        copy_len = DISCORD_EV_BODY_CAP - 1;
        truncated = 1;
    }
    if (copy_len > 0 && ctx->api_response_buf) {
        memcpy(ev.data.api_response.json_body, ctx->api_response_buf, copy_len);
    }
    ev.data.api_response.json_body[copy_len] = '\0';
    ev.data.api_response.body_len = copy_len;
    ev.data.api_response.truncated = truncated;
    (void)enqueue_event(ctx, &ev);
    /* Reset accumulator */
    ctx->api_response_len = 0;
    ctx->pending_status_code = 0;
}

/* Safe string copy into a fixed-size destination buffer */
static void safe_strcpy(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t len = strlen(src);
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* ═══════════════════════════════════════════════════════════════════════
 * REST event translation
 * ═══════════════════════════════════════════════════════════════════════ */

static void translate_rest_event(discord_ctx_t *ctx, const rest_event_t *rev) {
    if (!ctx || !rev) return;

    switch (rev->type) {
        case REST_EVENT_RESPONSE_HEADERS:
            /* Capture status code for the eventual API_RESPONSE event */
            ctx->api_response_len = 0;
            /* Reset rate limit tracking for new response */
            ctx->pending_retry_after_ms = 0;
            ctx->pending_ratelimit_remaining = -1;
            ctx->pending_ratelimit_reset_after_ms = 0;
            ctx->pending_ratelimit_global = 0;
            ctx->pending_ratelimit_bucket[0] = '\0';
            if (rev->data.status.status > 0) {
                ctx->pending_status_code = rev->data.status.status;
            }
            break;

        case REST_EVENT_HEADER_PROVIDED:
            /* Parse rate limit headers */
            if (rev->data.header.name && rev->data.header.value) {
                const char *name = rev->data.header.name;
                const char *val = rev->data.header.value;
                if (strcasecmp(name, "retry-after") == 0) {
                    /* Retry-After is seconds (float); convert to ms */
                    double secs = atof(val);
                    ctx->pending_retry_after_ms = (int)(secs * 1000.0);
                } else if (strcasecmp(name, "x-ratelimit-remaining") == 0) {
                    ctx->pending_ratelimit_remaining = atoi(val);
                } else if (strcasecmp(name, "x-ratelimit-reset-after") == 0) {
                    double secs = atof(val);
                    ctx->pending_ratelimit_reset_after_ms = (int)(secs * 1000.0);
                } else if (strcasecmp(name, "x-ratelimit-bucket") == 0) {
                    safe_strcpy(ctx->pending_ratelimit_bucket,
                                sizeof(ctx->pending_ratelimit_bucket), val);
                } else if (strcasecmp(name, "x-ratelimit-global") == 0) {
                    /* Value is "true" for global limits */
                    if (strcmp(val, "true") == 0) ctx->pending_ratelimit_global = 1;
                } else if (strcasecmp(name, "x-ratelimit-scope") == 0) {
                    /* "global" scope indicates global rate limit */
                    if (strcmp(val, "global") == 0) ctx->pending_ratelimit_global = 1;
                }
            }
            break;

        case REST_EVENT_RESPONSE_DATA:
            if (rev->data.data.data && rev->data.data.len > 0) {
                accumulate_api_response(ctx, rev->data.data.data, rev->data.data.len);
            }
            break;

        case REST_EVENT_RESPONSE_COMPLETE:
            emit_api_response(ctx);
            /* Emit rate limited event if status is 429 */
            if (ctx->pending_status_code == 429) {
                discord_event_t rlev;
                memset(&rlev, 0, sizeof(rlev));
                rlev.type = DISCORD_EVENT_RATE_LIMITED;
                rlev.data.rate_limit.retry_after_ms = ctx->pending_retry_after_ms;
                rlev.data.rate_limit.is_global = ctx->pending_ratelimit_global;
                safe_strcpy(rlev.data.rate_limit.bucket,
                            sizeof(rlev.data.rate_limit.bucket),
                            ctx->pending_ratelimit_bucket);
                (void)enqueue_event(ctx, &rlev);
            }
            break;

        case REST_EVENT_BINARY_CHANNEL_READY:
        {
            ctx->gateway_state = GATEWAY_STATE_CONNECTED;
            discord_event_t ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = DISCORD_EVENT_CONNECTED;
            (void)enqueue_event(ctx, &ev);
            break;
        }

        case REST_EVENT_WS_TEXT:
            if (rev->data.ws_message.data && rev->data.ws_message.len > 0) {
                translate_ws_text(ctx, (const char *)rev->data.ws_message.data,
                                  rev->data.ws_message.len);
            }
            break;

        case REST_EVENT_WS_BINARY:
        {
            discord_event_t ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = DISCORD_EVENT_RAW;
            (void)enqueue_event(ctx, &ev);
            break;
        }

        case REST_EVENT_WS_CLOSE:
        {
            ctx->gateway_state = GATEWAY_STATE_DISCONNECTED;
            discord_event_t ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = DISCORD_EVENT_DISCONNECTED;
            (void)enqueue_event(ctx, &ev);
            break;
        }

        case REST_EVENT_ERROR:
        {
            discord_event_t ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = DISCORD_EVENT_ERROR;
            if (rev->error_msg) {
                safe_strcpy(ev.error_msg, sizeof(ev.error_msg), rev->error_msg);
            } else {
                safe_strcpy(ev.error_msg, sizeof(ev.error_msg), "REST error");
            }
            (void)enqueue_event(ctx, &ev);
            break;
        }

        default:
            break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * Minimal JSON field extraction (no parser, pure string search)
 * ═══════════════════════════════════════════════════════════════════════ */

static int json_find_string(const char *json, size_t json_len,
                            const char *key, char *val_buf, size_t val_max) {
    if (!json || !key || !val_buf || val_max == 0) return 0;
    size_t klen = strlen(key);

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
                while (pos < json_len) {
                    if (json[pos] == '\\' && pos + 1 < json_len) {
                        pos += 2; /* skip escaped character */
                        continue;
                    }
                    if (json[pos] == '"') break;
                    pos++;
                }
                size_t vlen = pos - start;
                if (vlen >= val_max) vlen = val_max - 1;
                /* Copy, unescaping \" and \\ */
                size_t wi = 0;
                for (size_t ri = 0; ri < vlen && wi < val_max - 1; ri++) {
                    if (start + ri < json_len - 1 &&
                        json[start + ri] == '\\') {
                        char next = json[start + ri + 1];
                        if (next == '"' || next == '\\') {
                            val_buf[wi++] = next;
                            ri++; /* skip the escaped char */
                            continue;
                        }
                    }
                    val_buf[wi++] = json[start + ri];
                }
                val_buf[wi] = '\0';
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

/* ═══════════════════════════════════════════════════════════════════════
 * Gateway WebSocket text → Discord event translation
 * ═══════════════════════════════════════════════════════════════════════ */

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

    /* Temp buffer for JSON extraction */
    char tmp[DISCORD_EV_BODY_CAP];

    switch (op) {
        case 0: /* Dispatch */
            if (strcmp(event_name, "READY") == 0) {
                size_t dlen = 0;
                const char *d = json_find_object(text, len, "d", &dlen);
                if (d && dlen > 0) {
                    discord_event_t ev;
                    memset(&ev, 0, sizeof(ev));
                    ev.type = DISCORD_EVENT_READY;

                    json_find_string(d, dlen, "session_id",
                                     ev.data.ready.session_id, sizeof(ev.data.ready.session_id));
                    /* Cache session_id for resume */
                    safe_strcpy(ctx->session_id, sizeof(ctx->session_id),
                                ev.data.ready.session_id);

                    json_find_string(d, dlen, "resume_gateway_url",
                                     ev.data.ready.gateway_url, sizeof(ev.data.ready.gateway_url));
                    safe_strcpy(ctx->resume_gateway_url, sizeof(ctx->resume_gateway_url),
                                ev.data.ready.gateway_url);

                    size_t ulen = 0;
                    const char *uobj = json_find_object(d, dlen, "user", &ulen);
                    if (uobj && ulen > 0) {
                        json_find_string(uobj, ulen, "id",
                                         ev.data.ready.user_id, sizeof(ev.data.ready.user_id));
                    }

                    ctx->gateway_state = GATEWAY_STATE_IDENTIFIED;
                    (void)enqueue_event(ctx, &ev);
                }
            } else if (strcmp(event_name, "MESSAGE_CREATE") == 0) {
                size_t dlen = 0;
                const char *d = json_find_object(text, len, "d", &dlen);
                if (d && dlen > 0) {
                    discord_event_t ev;
                    memset(&ev, 0, sizeof(ev));
                    ev.type = DISCORD_EVENT_MESSAGE_CREATE;

                    json_find_string(d, dlen, "id",
                        ev.data.message.id, sizeof(ev.data.message.id));
                    json_find_string(d, dlen, "channel_id",
                        ev.data.message.channel_id, sizeof(ev.data.message.channel_id));
                    json_find_string(d, dlen, "guild_id",
                        ev.data.message.guild_id, sizeof(ev.data.message.guild_id));
                    json_find_string(d, dlen, "content",
                        ev.data.message.content, sizeof(ev.data.message.content));
                    json_find_string(d, dlen, "timestamp",
                        ev.data.message.timestamp, sizeof(ev.data.message.timestamp));

                    size_t alen = 0;
                    const char *aobj = json_find_object(d, dlen, "author", &alen);
                    if (aobj && alen > 0) {
                        json_find_string(aobj, alen, "id",
                            ev.data.message.author_id, sizeof(ev.data.message.author_id));
                    }

                    (void)enqueue_event(ctx, &ev);
                }
            } else if (strcmp(event_name, "MESSAGE_UPDATE") == 0) {
                size_t dlen = 0;
                const char *d = json_find_object(text, len, "d", &dlen);
                if (d && dlen > 0) {
                    discord_event_t ev;
                    memset(&ev, 0, sizeof(ev));
                    ev.type = DISCORD_EVENT_MESSAGE_UPDATE;
                    json_find_string(d, dlen, "id",
                        ev.data.message.id, sizeof(ev.data.message.id));
                    json_find_string(d, dlen, "channel_id",
                        ev.data.message.channel_id, sizeof(ev.data.message.channel_id));
                    json_find_string(d, dlen, "content",
                        ev.data.message.content, sizeof(ev.data.message.content));
                    (void)enqueue_event(ctx, &ev);
                }
            } else if (strcmp(event_name, "MESSAGE_DELETE") == 0) {
                size_t dlen = 0;
                const char *d = json_find_object(text, len, "d", &dlen);
                if (d && dlen > 0) {
                    discord_event_t ev;
                    memset(&ev, 0, sizeof(ev));
                    ev.type = DISCORD_EVENT_MESSAGE_DELETE;
                    json_find_string(d, dlen, "id",
                        ev.data.message.id, sizeof(ev.data.message.id));
                    json_find_string(d, dlen, "channel_id",
                        ev.data.message.channel_id, sizeof(ev.data.message.channel_id));
                    (void)enqueue_event(ctx, &ev);
                }
            } else if (strcmp(event_name, "CHANNEL_CREATE") == 0 ||
                       strcmp(event_name, "CHANNEL_UPDATE") == 0 ||
                       strcmp(event_name, "CHANNEL_DELETE") == 0) {
                discord_event_type_t etype = DISCORD_EVENT_CHANNEL_CREATE;
                if (event_name[8] == 'U') etype = DISCORD_EVENT_CHANNEL_UPDATE;
                else if (event_name[8] == 'D') etype = DISCORD_EVENT_CHANNEL_DELETE;
                discord_event_t ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = etype;
                (void)enqueue_event(ctx, &ev);
            } else if (strcmp(event_name, "GUILD_CREATE") == 0) {
                discord_event_t ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = DISCORD_EVENT_GUILD_CREATE;
                (void)enqueue_event(ctx, &ev);
            } else if (strcmp(event_name, "GUILD_UPDATE") == 0) {
                discord_event_t ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = DISCORD_EVENT_GUILD_UPDATE;
                (void)enqueue_event(ctx, &ev);
            } else if (strcmp(event_name, "GUILD_DELETE") == 0) {
                discord_event_t ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = DISCORD_EVENT_GUILD_DELETE;
                (void)enqueue_event(ctx, &ev);
            } else if (strcmp(event_name, "GUILD_MEMBER_ADD") == 0 ||
                       strcmp(event_name, "GUILD_MEMBER_REMOVE") == 0 ||
                       strcmp(event_name, "GUILD_MEMBER_UPDATE") == 0) {
                discord_event_type_t etype = DISCORD_EVENT_GUILD_MEMBER_ADD;
                if (event_name[13] == 'R') etype = DISCORD_EVENT_GUILD_MEMBER_REMOVE;
                else if (event_name[13] == 'U') etype = DISCORD_EVENT_GUILD_MEMBER_UPDATE;
                discord_event_t ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = etype;
                (void)enqueue_event(ctx, &ev);
            } else if (strcmp(event_name, "GUILD_ROLE_CREATE") == 0 ||
                       strcmp(event_name, "GUILD_ROLE_UPDATE") == 0 ||
                       strcmp(event_name, "GUILD_ROLE_DELETE") == 0) {
                discord_event_type_t etype = DISCORD_EVENT_GUILD_ROLE_CREATE;
                if (event_name[11] == 'U') etype = DISCORD_EVENT_GUILD_ROLE_UPDATE;
                else if (event_name[11] == 'D') etype = DISCORD_EVENT_GUILD_ROLE_DELETE;
                discord_event_t ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = etype;
                (void)enqueue_event(ctx, &ev);
            } else if (strcmp(event_name, "MESSAGE_REACTION_ADD") == 0) {
                discord_event_t ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = DISCORD_EVENT_MESSAGE_REACTION_ADD;
                (void)enqueue_event(ctx, &ev);
            } else if (strcmp(event_name, "MESSAGE_REACTION_REMOVE") == 0) {
                discord_event_t ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = DISCORD_EVENT_MESSAGE_REACTION_REMOVE;
                (void)enqueue_event(ctx, &ev);
            } else if (strcmp(event_name, "PRESENCE_UPDATE") == 0) {
                discord_event_t ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = DISCORD_EVENT_PRESENCE_UPDATE;
                (void)enqueue_event(ctx, &ev);
            } else if (strcmp(event_name, "TYPING_START") == 0) {
                discord_event_t ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = DISCORD_EVENT_TYPING_START;
                (void)enqueue_event(ctx, &ev);
            } else if (strcmp(event_name, "VOICE_STATE_UPDATE") == 0) {
                discord_event_t ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = DISCORD_EVENT_VOICE_STATE_UPDATE;
                (void)enqueue_event(ctx, &ev);
            } else if (strcmp(event_name, "THREAD_CREATE") == 0 ||
                       strcmp(event_name, "THREAD_UPDATE") == 0 ||
                       strcmp(event_name, "THREAD_DELETE") == 0) {
                discord_event_type_t etype = DISCORD_EVENT_THREAD_CREATE;
                if (event_name[7] == 'U') etype = DISCORD_EVENT_THREAD_UPDATE;
                else if (event_name[7] == 'D') etype = DISCORD_EVENT_THREAD_DELETE;
                discord_event_t ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = etype;
                (void)enqueue_event(ctx, &ev);
            } else if (strcmp(event_name, "INTERACTION_CREATE") == 0) {
                size_t dlen = 0;
                const char *d = json_find_object(text, len, "d", &dlen);
                if (d && dlen > 0) {
                    discord_event_t ev;
                    memset(&ev, 0, sizeof(ev));
                    ev.type = DISCORD_EVENT_INTERACTION_CREATE;

                    json_find_string(d, dlen, "id",
                        ev.data.interaction.id, sizeof(ev.data.interaction.id));
                    json_find_string(d, dlen, "token",
                        ev.data.interaction.token, sizeof(ev.data.interaction.token));
                    json_find_string(d, dlen, "application_id",
                        ev.data.interaction.application_id,
                        sizeof(ev.data.interaction.application_id));
                    ev.data.interaction.type = 0;
                    json_find_int(d, dlen, "type", &ev.data.interaction.type);

                    /* Copy raw data field as JSON */
                    size_t ddlen = 0;
                    const char *dd = json_find_object(d, dlen, "data", &ddlen);
                    if (dd && ddlen > 0) {
                        if (ddlen >= DISCORD_EV_BODY_CAP) ddlen = DISCORD_EV_BODY_CAP - 1;
                        memcpy(ev.data.interaction.data_json, dd, ddlen);
                        ev.data.interaction.data_json[ddlen] = '\0';
                    }

                    (void)enqueue_event(ctx, &ev);
                }
            }
            break;

        case 1: /* Heartbeat request from server */
        {
            discord_event_t ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = DISCORD_EVENT_HEARTBEAT_REQUEST;
            (void)enqueue_event(ctx, &ev);
            break;
        }

        case 7: /* Reconnect — server requests client reconnect */
            ctx->gateway_state = GATEWAY_STATE_DISCONNECTED;
            {
                discord_event_t ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = DISCORD_EVENT_RECONNECT_REQUEST;
                (void)enqueue_event(ctx, &ev);
            }
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
            ctx->gateway_state = GATEWAY_STATE_CONNECTED;
            break;
        }

        case 11: /* Heartbeat ACK */
        {
            discord_event_t ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = DISCORD_EVENT_HEARTBEAT_ACK;
            (void)enqueue_event(ctx, &ev);
            break;
        }

        default:
            break;
    }

    /* Suppress unused variable warning */
    (void)tmp;
}
