# ARCHITECTURE.md — libdiscord

## Overview

libdiscord is a pure-C Discord bot support library built on top of librest and shaggy (libhttp2).
It acts as thin plumbing: it provides Discord REST API v10 helpers and Gateway (WebSocket) support,
emits structured `discord_event_t` events, and delegates all I/O and policy to the caller.

## Module Boundaries

### `include/discord.h` — Public API
- Opaque `discord_ctx_t` context
- Event types: `discord_event_type_t` (READY, MESSAGE_CREATE, API_RESPONSE, GUILD_CREATE, etc.)
- Payload structs: `discord_message_t`, `discord_ready_t`, `discord_api_response_t`
- Config: `discord_config_t` (event_queue_size, bot_token, gateway_url, api_base_url)
- Role: `discord_role_t` (DISCORD_ROLE_CLIENT for bots, DISCORD_ROLE_SERVER for webhook receivers)
- Core API: `discord_create`, `discord_destroy`, `discord_feed_input`, `discord_next_event`, `discord_get_output`
- REST helpers: `discord_create_message`, `discord_edit_message`, `discord_delete_message`, `discord_get_channel`
- Gateway: `discord_gateway_connect`, `discord_gateway_send_heartbeat`, `discord_gateway_send_identify`, `discord_gateway_send_resume`

### `include/discord_types.h` — Discord Type Definitions
- Permission flags (future use)
- Channel types (future use)
- Message types (future use)
- Minimal for now; expanded as features grow

### `src/discord.c` — Core Implementation
- Context struct with `rest_ctx_t*`, event ring buffer, config, auth state, gateway state
- create/destroy lifecycle (creates `rest_ctx_t` internally)
- Event queue (enqueue_event, discord_next_event)
- `discord_feed_input` delegates to `rest_feed_input`
- `discord_get_output` delegates to `rest_get_output`
- REST helpers: `discord_create_message` → `rest_post_json("/channels/{id}/messages", ...)`
- REST helpers: `discord_edit_message` → `rest_put("/channels/{id}/messages/{msg_id}", ...)`
- REST helpers: `discord_delete_message` → `rest_delete("/channels/{id}/messages/{msg_id}", ...)`
- REST helpers: `discord_get_channel` → `rest_get("/channels/{id}", ...)`
- Gateway helpers: connect via binary channel, heartbeat timer, identify with token
- Event translation: REST events → Discord events, Gateway WS frames → Discord events
- Gateway heartbeat management (caller invokes `discord_gateway_process_heartbeat` periodically)

## Key Invariants

1. **No syscalls, no callbacks, no hidden I/O** — All networking lives in the caller.
2. **Event-driven** — Caller pulls events via `discord_next_event()`; library never pushes.
3. **Pure state machine** — Transitions driven only by input buffers and caller context.
4. **Plumbing only** — Library encodes/decodes Discord protocol; caller owns all policy and decisions.
5. **Strict warnings** — Compiles cleanly with `-Wall -Wextra -Wpedantic -Werror`.
6. **No direct HTTP/2 framing** — All HTTP/2 frame encoding/decoding delegated to librest/shaggy.

## Data Flow

### REST API (e.g. Create Message)
```
discord_create_message(ctx, channel_id, content)
  → Builds Authorization header ("Bot {token}")
  → Calls rest_post_json("/channels/{channel_id}/messages", json_body, headers)
  → REST generates HTTP/2 HEADERS + DATA frames via HPACK
  → Output available via discord_get_output → rest_get_output
  → Event: DISCORD_EVENT_API_RESPONSE when response arrives
```

### Gateway (WebSocket Real-Time)
```
discord_gateway_connect(ctx)
  → Calls rest_establish_binary_channel(ctx->rest)
  → REST generates extended CONNECT HEADERS frame
  → Output available via discord_get_output

discord_gateway_send_identify(ctx)
  → Builds identify JSON payload with token, intents, properties
  → Calls rest_ws_send_text(ctx->rest, identify_json)
  → Gateway emits READY event when server responds

discord_gateway_send_heartbeat(ctx, last_sequence)
  → Builds heartbeat JSON payload
  → Calls rest_ws_send_text(ctx->rest, heartbeat_json)

Gateway events arrive via discord_feed_input → rest_feed_input:
  REST_EVENT_WS_TEXT → parse JSON → emit discord_event_t
  REST_EVENT_WS_BINARY → emit DISCORD_EVENT_RAW
  REST_EVENT_WS_CLOSE → emit DISCORD_EVENT_DISCONNECTED
```

### Event Translation
```
REST_EVENT_RESPONSE_HEADERS  → DISCORD_EVENT_API_RESPONSE (with status)
REST_EVENT_RESPONSE_DATA     → accumulate JSON body
REST_EVENT_RESPONSE_COMPLETE → emit complete API response
REST_EVENT_WS_TEXT           → parse Gateway JSON → emit typed Discord event
REST_EVENT_WS_CLOSE          → DISCORD_EVENT_DISCONNECTED
REST_EVENT_BINARY_CHANNEL_READY → DISCORD_EVENT_CONNECTED
REST_EVENT_ERROR             → DISCORD_EVENT_ERROR
```

### Output Retrieval
```
discord_get_output(buf, max):
  Delegates directly to rest_get_output (rest handles shaggy delegation)
```

## Librest Integration

- libdiscord owns a `rest_ctx_t` per context (created via `rest_create_with_config`)
- Input: `discord_feed_input` delegates to `rest_feed_input`; REST/Gateway events polled via `rest_next_event`
- Output: `discord_get_output` delegates to `rest_get_output`
- REST API calls use librest CRUD helpers (`rest_post_json`, `rest_put`, `rest_delete`, `rest_get`)
- Gateway uses librest binary channel (`rest_establish_binary_channel`, `rest_ws_send_text`)
- Discord-specific auth headers (Bot token) injected via librest's header interface

## Heartbeat Management

- Gateway requires periodic heartbeats to maintain connection
- `discord_gateway_process_heartbeat(ctx)` checks if heartbeat interval has elapsed
- Caller must invoke this periodically (pure function, no timers, no threads)
- Heartbeat payload includes last sequence number for resume support

## Deliberate Absences

- **No JSON parser** — Library emits raw JSON strings; caller owns parsing
- **No event cache** — No in-memory cache of guilds, channels, or members
- **No rate limiting** — Caller implements rate limit handling
- **No voice support** — Voice is a separate protocol; out of scope
- **No shard management** — Caller manages sharding strategy
- **No OAuth2** — Bot token auth only

## Testing Strategy

- **Smoke test** (`test_discord_smoke.c`): create/destroy lifecycle, NULL handling, config validation, event queue drain, API helper NULL checks
- **Dialectic test** (`test_discord_dialectic.c`): paired client↔server exchange simulating Discord API round-trip and Gateway handshake
- Both pass under Valgrind with zero leaks and zero errors
