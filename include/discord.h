#ifndef DISCORD_H
#define DISCORD_H

#include <stddef.h>
#include <stdint.h>
#include "discord_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── String/buffer size constants ────────────────────────────────────── */
#define DISCORD_ID_LEN          64
#define DISCORD_MSG_CONTENT_LEN 2048
#define DISCORD_TIMESTAMP_LEN   64
#define DISCORD_URL_LEN         512
#define DISCORD_SESSION_ID_LEN  128
#define DISCORD_EV_BODY_CAP     8192
#define DISCORD_ERROR_MSG_LEN   256

/* ── Role for dialectic client/server usage ──────────────────────────── */
typedef enum {
    DISCORD_ROLE_CLIENT = 0,  /* bot client */
    DISCORD_ROLE_SERVER = 1   /* webhook receiver */
} discord_role_t;

/* ── Config struct (ADR 009 consistent interface pattern) ────────────── */
typedef struct {
    size_t      event_queue_size;  /* configurable event queue depth */
    const char *bot_token;         /* Discord bot token (stored internally) */
    const char *gateway_url;       /* gateway URL override (NULL = default) */
    const char *api_base_url;      /* API base URL override (NULL = default) */
    int         intents;           /* Gateway intents bitfield (0 = all non-privileged) */
    int         shard_id;          /* shard index (only used when num_shards > 0) */
    int         num_shards;        /* total shards (0 = no sharding) */
} discord_config_t;

/* ── Opaque context ──────────────────────────────────────────────────── */
typedef struct discord_ctx discord_ctx_t;

/* ── Discord-specific event types ────────────────────────────────────── */
typedef enum {
    DISCORD_EVENT_NONE = 0,
    DISCORD_EVENT_ERROR,

    /* Gateway lifecycle */
    DISCORD_EVENT_READY,             /* Gateway READY received (session + user info) */
    DISCORD_EVENT_CONNECTED,         /* Binary channel established (Gateway or API) */
    DISCORD_EVENT_DISCONNECTED,      /* Gateway disconnected (WS close) */
    DISCORD_EVENT_HEARTBEAT_REQUEST, /* Server requests heartbeat (op 1) */
    DISCORD_EVENT_HEARTBEAT_ACK,    /* Server acknowledges heartbeat (op 11) */
    DISCORD_EVENT_RECONNECT_REQUEST,/* Server requests reconnect (op 7) */

    /* Message events */
    DISCORD_EVENT_MESSAGE_CREATE,    /* New message received */
    DISCORD_EVENT_MESSAGE_UPDATE,    /* Message edited */
    DISCORD_EVENT_MESSAGE_DELETE,    /* Message deleted */

    /* Channel events */
    DISCORD_EVENT_CHANNEL_CREATE,
    DISCORD_EVENT_CHANNEL_UPDATE,
    DISCORD_EVENT_CHANNEL_DELETE,

    /* Guild events */
    DISCORD_EVENT_GUILD_CREATE,      /* Guild (server) info received */
    DISCORD_EVENT_GUILD_UPDATE,
    DISCORD_EVENT_GUILD_DELETE,

    /* Guild member events (privileged: GUILD_MEMBERS intent) */
    DISCORD_EVENT_GUILD_MEMBER_ADD,
    DISCORD_EVENT_GUILD_MEMBER_REMOVE,
    DISCORD_EVENT_GUILD_MEMBER_UPDATE,

    /* Guild role events */
    DISCORD_EVENT_GUILD_ROLE_CREATE,
    DISCORD_EVENT_GUILD_ROLE_UPDATE,
    DISCORD_EVENT_GUILD_ROLE_DELETE,

    /* Reaction events */
    DISCORD_EVENT_MESSAGE_REACTION_ADD,
    DISCORD_EVENT_MESSAGE_REACTION_REMOVE,

    /* Presence / typing / voice */
    DISCORD_EVENT_PRESENCE_UPDATE,
    DISCORD_EVENT_TYPING_START,
    DISCORD_EVENT_VOICE_STATE_UPDATE,

    /* Thread events */
    DISCORD_EVENT_THREAD_CREATE,
    DISCORD_EVENT_THREAD_UPDATE,
    DISCORD_EVENT_THREAD_DELETE,

    /* Interaction events */
    DISCORD_EVENT_INTERACTION_CREATE,/* Slash command / component interaction */

    /* API response */
    DISCORD_EVENT_API_RESPONSE,      /* REST API response received */

    /* Rate limiting */
    DISCORD_EVENT_RATE_LIMITED,      /* HTTP 429 rate limited (retry_after_ms, is_global) */

    /* Raw / passthrough */
    DISCORD_EVENT_RAW                /* Raw WebSocket frame (binary or unparseable text) */
} discord_event_type_t;

/* ── Discord message payload ─────────────────────────────────────────── */
typedef struct {
    char id[DISCORD_ID_LEN];              /* snowflake ID string */
    char channel_id[DISCORD_ID_LEN];      /* channel snowflake */
    char guild_id[DISCORD_ID_LEN];        /* guild snowflake (empty for DMs) */
    char author_id[DISCORD_ID_LEN];       /* author user snowflake */
    char content[DISCORD_MSG_CONTENT_LEN];/* message content (UTF-8) */
    char timestamp[DISCORD_TIMESTAMP_LEN];/* ISO 8601 timestamp */
} discord_message_t;

/* ── Discord READY event payload ─────────────────────────────────────── */
typedef struct {
    char user_id[DISCORD_ID_LEN];         /* bot user snowflake */
    char session_id[DISCORD_SESSION_ID_LEN]; /* gateway session ID */
    char gateway_url[DISCORD_URL_LEN];    /* resume gateway URL */
} discord_ready_t;

/* ── API response payload ────────────────────────────────────────────── */
typedef struct {
    int    status_code;                    /* HTTP status code */
    char   json_body[DISCORD_EV_BODY_CAP]; /* raw JSON response body */
    size_t body_len;                       /* bytes stored in json_body */
    int    truncated;                      /* 1 if body exceeded buffer */
} discord_api_response_t;

/* ── Interaction payload ─────────────────────────────────────────────── */
typedef struct {
    char id[DISCORD_ID_LEN];              /* interaction snowflake */
    int  type;                             /* 1=ping, 2=app_cmd, 3=component, 5=modal */
    char token[DISCORD_ID_LEN];           /* interaction token for responses */
    char application_id[DISCORD_ID_LEN];  /* application snowflake */
    char data_json[DISCORD_EV_BODY_CAP];  /* raw JSON of data field */
} discord_interaction_t;

/* ── Rate limit payload ──────────────────────────────────────────────── */
typedef struct {
    int      retry_after_ms;               /* milliseconds until rate limit resets */
    int      is_global;                    /* 1 if global rate limit, 0 if per-route */
    char     bucket[DISCORD_ID_LEN];       /* rate limit bucket (from X-RateLimit-Bucket) */
} discord_rate_limit_t;

/* ── Main event struct (self-contained, copy-safe) ───────────────────── */
typedef struct {
    discord_event_type_t type;
    union {
        discord_message_t       message;       /* MESSAGE_CREATE, MESSAGE_UPDATE, MESSAGE_DELETE */
        discord_ready_t         ready;         /* READY */
        discord_api_response_t  api_response;  /* API_RESPONSE */
        discord_interaction_t   interaction;   /* INTERACTION_CREATE */
        discord_rate_limit_t    rate_limit;    /* RATE_LIMITED */
    } data;
    char error_msg[DISCORD_ERROR_MSG_LEN];     /* ERROR detail string */
} discord_event_t;

/* ── Gateway state (public) ──────────────────────────────────────────── */
typedef enum {
    DISCORD_GW_DISCONNECTED = 0,
    DISCORD_GW_CONNECTING,
    DISCORD_GW_CONNECTED,
    DISCORD_GW_IDENTIFIED,
    DISCORD_GW_RESUMING
} discord_gateway_state_t;

/* ── Default intents: all non-privileged Gateway intents ─────────────── */
#define DISCORD_INTENTS_DEFAULT \
    (DISCORD_INTENT_GUILDS | DISCORD_INTENT_GUILD_MODERATION | \
     DISCORD_INTENT_GUILD_EXPRESSIONS | DISCORD_INTENT_GUILD_INTEGRATIONS | \
     DISCORD_INTENT_GUILD_WEBHOOKS | DISCORD_INTENT_GUILD_INVITES | \
     DISCORD_INTENT_GUILD_VOICE_STATES | DISCORD_INTENT_GUILD_MESSAGES | \
     DISCORD_INTENT_GUILD_MESSAGE_REACTIONS | DISCORD_INTENT_GUILD_MESSAGE_TYPING | \
     DISCORD_INTENT_DIRECT_MESSAGES | DISCORD_INTENT_DIRECT_MESSAGE_REACTIONS | \
     DISCORD_INTENT_DIRECT_MESSAGE_TYPING | DISCORD_INTENT_GUILD_SCHEDULED_EVENTS | \
     DISCORD_INTENT_AUTO_MODERATION_CONFIGURATION | \
     DISCORD_INTENT_AUTO_MODERATION_EXECUTION)

/* ══════════════════════════════════════════════════════════════════════
 * Core API — consistent with ADR 009 / common interface contract
 * ══════════════════════════════════════════════════════════════════════ */

discord_ctx_t *discord_create(discord_role_t role);
discord_ctx_t *discord_create_with_config(discord_role_t role, const discord_config_t *config);
void           discord_destroy(discord_ctx_t *ctx);

int  discord_feed_input(discord_ctx_t *ctx, const uint8_t *data, size_t len);
int  discord_next_event(discord_ctx_t *ctx, discord_event_t *event);  /* returns 1 if event available */
int  discord_get_output(discord_ctx_t *ctx, uint8_t *buf, size_t max);

/* ── Post-creation configuration ─────────────────────────────────────── */

/* Update bot token (e.g. for token rotation). Rebuilds auth header. */
int discord_set_token(discord_ctx_t *ctx, const char *token);

/* Reset internal state for reuse (clears queue, gateway state, session).
 * Keeps bot_token, config, and librest context. */
void discord_reset(discord_ctx_t *ctx);

/* ══════════════════════════════════════════════════════════════════════
 * REST API helpers — thin convenience wrappers over librest.
 * All return 0 on success, -1 on error.
 * Caller owns I/O — helpers only generate HTTP/2 frames via librest.
 * ══════════════════════════════════════════════════════════════════════ */

/* POST /channels/{channel_id}/messages with JSON body {"content": "..."} */
int discord_create_message(discord_ctx_t *ctx, const char *channel_id, const char *content);

/* POST /channels/{channel_id}/messages — extended version with embeds/components.
 * embeds_json and components_json are raw JSON strings or NULL. */
int discord_create_message_ex(discord_ctx_t *ctx, const char *channel_id,
                              const char *content, const char *embeds_json,
                              const char *components_json);

/* PUT /channels/{channel_id}/messages/{message_id} with JSON body {"content": "..."} */
int discord_edit_message(discord_ctx_t *ctx, const char *channel_id,
                         const char *message_id, const char *content);

/* DELETE /channels/{channel_id}/messages/{message_id} */
int discord_delete_message(discord_ctx_t *ctx, const char *channel_id,
                           const char *message_id);

/* GET /channels/{channel_id} */
int discord_get_channel(discord_ctx_t *ctx, const char *channel_id);

/* GET /guilds/{guild_id} */
int discord_get_guild(discord_ctx_t *ctx, const char *guild_id);

/* GET /channels/{channel_id}/messages */
int discord_get_channel_messages(discord_ctx_t *ctx, const char *channel_id);

/* PUT /channels/{cid}/messages/{mid}/reactions/{emoji}/@me */
int discord_add_reaction(discord_ctx_t *ctx, const char *channel_id,
                         const char *message_id, const char *emoji);

/* DELETE /channels/{cid}/messages/{mid}/reactions/{emoji}/@me */
int discord_delete_own_reaction(discord_ctx_t *ctx, const char *channel_id,
                                const char *message_id, const char *emoji);

/* POST /interactions/{id}/{token}/callback with raw JSON body */
int discord_create_interaction_response(discord_ctx_t *ctx,
                                        const char *interaction_id,
                                        const char *interaction_token,
                                        const char *json);

/* POST /users/@me/channels with {"recipient_id": "..."} */
int discord_create_dm(discord_ctx_t *ctx, const char *recipient_id);

/* ══════════════════════════════════════════════════════════════════════
 * Gateway helpers — WebSocket via librest binary channel.
 * All return 0 on success, -1 on error.
 * ══════════════════════════════════════════════════════════════════════ */

/* Establish Gateway WebSocket connection (extended CONNECT via librest) */
int discord_gateway_connect(discord_ctx_t *ctx);

/* Send Gateway identify payload (token, intents, properties) */
int discord_gateway_send_identify(discord_ctx_t *ctx);

/* Send Gateway heartbeat with last sequence number.
 * now_ms: caller-supplied monotonic timestamp for timing tracking. */
int discord_gateway_send_heartbeat(discord_ctx_t *ctx, int last_sequence, uint64_t now_ms);

/* Send Gateway resume payload (token, session_id, last_sequence) */
int discord_gateway_send_resume(discord_ctx_t *ctx);

/* Process heartbeat timing — caller invokes periodically with monotonic timestamp.
 * Returns 1 if heartbeat should be sent now, 0 otherwise.
 * now_ms: caller-supplied monotonic millisecond timestamp. */
int discord_gateway_process_heartbeat(discord_ctx_t *ctx, uint64_t now_ms);

/* Query current gateway connection state. */
discord_gateway_state_t discord_gateway_state(discord_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* DISCORD_H */
