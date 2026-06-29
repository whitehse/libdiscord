#ifndef DISCORD_H
#define DISCORD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Role for dialectic client/server usage */
typedef enum {
    DISCORD_ROLE_CLIENT = 0,  /* bot client */
    DISCORD_ROLE_SERVER = 1   /* webhook receiver */
} discord_role_t;

/* Config struct - follows ADR 009 consistent interface pattern */
typedef struct {
    size_t event_queue_size;      /* configurable event queue depth */
    const char *bot_token;        /* Discord bot token (stored internally) */
    const char *gateway_url;      /* gateway URL override (NULL = default) */
    const char *api_base_url;     /* API base URL override (NULL = default) */
} discord_config_t;

/* Opaque context */
typedef struct discord_ctx discord_ctx_t;

/* Discord-specific event types */
typedef enum {
    DISCORD_EVENT_NONE = 0,
    DISCORD_EVENT_ERROR,

    /* Gateway lifecycle */
    DISCORD_EVENT_READY,             /* Gateway READY received (session + user info) */
    DISCORD_EVENT_CONNECTED,         /* Binary channel established (Gateway or API) */
    DISCORD_EVENT_DISCONNECTED,      /* Gateway disconnected (WS close) */

    /* Message events */
    DISCORD_EVENT_MESSAGE_CREATE,    /* New message received */
    DISCORD_EVENT_MESSAGE_UPDATE,    /* Message edited */
    DISCORD_EVENT_MESSAGE_DELETE,    /* Message deleted */

    /* Guild events */
    DISCORD_EVENT_GUILD_CREATE,      /* Guild (server) info received */

    /* Interaction events */
    DISCORD_EVENT_INTERACTION_CREATE,/* Slash command / component interaction */

    /* API response */
    DISCORD_EVENT_API_RESPONSE,      /* REST API response received */

    /* Raw / passthrough */
    DISCORD_EVENT_RAW                /* Raw WebSocket frame (binary or unparseable text) */
} discord_event_type_t;

/* Discord message payload */
typedef struct {
    const char *id;           /* snowflake ID string */
    const char *channel_id;   /* channel snowflake */
    const char *guild_id;     /* guild snowflake (NULL for DMs) */
    const char *author_id;    /* author user snowflake */
    const char *content;      /* message content (UTF-8) */
    const char *timestamp;    /* ISO 8601 timestamp */
} discord_message_t;

/* Discord READY event payload */
typedef struct {
    const char *user_id;      /* bot user snowflake */
    const char *session_id;   /* gateway session ID */
    const char *gateway_url;  /* resume gateway URL */
} discord_ready_t;

/* API response payload */
typedef struct {
    int status_code;          /* HTTP status code (200, 201, 204, 400, 401, 403, 404, 429, etc.) */
    const char *json_body;    /* raw JSON response body */
    size_t body_len;          /* length of json_body */
} discord_api_response_t;

/* Main event struct */
typedef struct {
    discord_event_type_t type;
    union {
        discord_message_t       message;       /* MESSAGE_CREATE, MESSAGE_UPDATE, MESSAGE_DELETE */
        discord_ready_t         ready;         /* READY */
        discord_api_response_t  api_response;  /* API_RESPONSE */
    } data;
    const char *error_msg;                      /* ERROR detail string */
} discord_event_t;

/* Core API - consistent with ADR 009 / common interface contract */
discord_ctx_t *discord_create(discord_role_t role);
discord_ctx_t *discord_create_with_config(discord_role_t role, const discord_config_t *config);
void           discord_destroy(discord_ctx_t *ctx);

int  discord_feed_input(discord_ctx_t *ctx, const uint8_t *data, size_t len);
int  discord_next_event(discord_ctx_t *ctx, discord_event_t *event);  /* returns 1 if event available */
int  discord_get_output(discord_ctx_t *ctx, uint8_t *buf, size_t max);

/* REST API helpers — thin convenience wrappers over librest.
 * All return 0 on success, -1 on error.
 * Caller owns I/O — helpers only generate HTTP/2 frames via librest. */

/* POST /channels/{channel_id}/messages with JSON body {"content": "..."} */
int discord_create_message(discord_ctx_t *ctx, const char *channel_id, const char *content);

/* PUT /channels/{channel_id}/messages/{message_id} with JSON body {"content": "..."} */
int discord_edit_message(discord_ctx_t *ctx, const char *channel_id,
                         const char *message_id, const char *content);

/* DELETE /channels/{channel_id}/messages/{message_id} */
int discord_delete_message(discord_ctx_t *ctx, const char *channel_id,
                           const char *message_id);

/* GET /channels/{channel_id} */
int discord_get_channel(discord_ctx_t *ctx, const char *channel_id);

/* Gateway helpers — WebSocket via librest binary channel.
 * All return 0 on success, -1 on error. */

/* Establish Gateway WebSocket connection (extended CONNECT via librest) */
int discord_gateway_connect(discord_ctx_t *ctx);

/* Send Gateway identify payload (token, intents, properties) */
int discord_gateway_send_identify(discord_ctx_t *ctx);

/* Send Gateway heartbeat with last sequence number */
int discord_gateway_send_heartbeat(discord_ctx_t *ctx, int last_sequence);

/* Send Gateway resume payload (token, session_id, last_sequence) */
int discord_gateway_send_resume(discord_ctx_t *ctx);

/* Process heartbeat timing — caller invokes periodically (pure function, no timers).
 * Returns 1 if heartbeat should be sent now, 0 otherwise. */
int discord_gateway_process_heartbeat(discord_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* DISCORD_H */
