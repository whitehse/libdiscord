# ARCHITECTURE.md — libdiscord

## Overview

libdiscord is a pure-C Discord bot support library built on top of librest and shaggy (libhttp2).
It acts as thin plumbing: it provides Discord REST API v10 helpers and Gateway (WebSocket) support,
emits structured `discord_event_t` events, and delegates all I/O and policy to the caller.

## Module Boundaries

### `include/discord.h` — Public API
- Opaque `discord_ctx_t` context
- Config: `discord_config_t` (event_queue_size, bot_token, gateway_url, api_base_url, intents, shard_id, num_shards)
- Role: `discord_role_t` (DISCORD_ROLE_CLIENT for bots, DISCORD_ROLE_SERVER for webhook receivers)
- Gateway state: `discord_gateway_state_t` (DISCONNECTED, CONNECTING, CONNECTED, IDENTIFIED, RESUMING)
- Event types: `discord_event_type_t` (34 types covering lifecycle, messages, channels, guilds, members, roles, reactions, presence, typing, voice, threads, interactions, API responses)
- Payload structs (all self-contained, copy-safe):
  - `discord_message_t` — embedded char arrays for id, channel_id, guild_id, author_id, content, timestamp
  - `discord_ready_t` — embedded char arrays for user_id, session_id, gateway_url
  - `discord_api_response_t` — status_code, embedded json_body buffer, body_len, truncated flag
  - `discord_interaction_t` — id, type, token, application_id, embedded data_json buffer
- Core API: `discord_create`, `discord_destroy`, `discord_feed_input`, `discord_next_event`, `discord_get_output`
- Post-creation: `discord_set_token`, `discord_reset`
- REST helpers: `discord_create_message`, `discord_create_message_ex`, `discord_edit_message`, `discord_delete_message`, `discord_get_channel`, `discord_get_guild`, `discord_get_channel_messages`, `discord_add_reaction`, `discord_delete_own_reaction`, `discord_create_interaction_response`, `discord_create_dm`
- Gateway: `discord_gateway_connect`, `discord_gateway_send_heartbeat`, `discord_gateway_send_identify`, `discord_gateway_send_resume`, `discord_gateway_process_heartbeat`, `discord_gateway_state`

### `include/discord_types.h` — Discord Type Definitions
- Permission flags (`discord_permission_t`)
- Channel types (`discord_channel_type_t`)
- Message types (`discord_message_type_t`)
- Gateway intents (`discord_intent_t`) — used in `discord_config_t.intents` and identify payload

### `src/discord.c` — Core Implementation
- Context struct with `rest_ctx_t*`, event ring buffer, config, auth state, gateway state
- create/destroy lifecycle (creates `rest_ctx_t` internally)
- Event queue (enqueue_event, discord_next_event)
- `discord_feed_input` delegates to `rest_feed_input`
- `discord_get_output` delegates to `rest_get_output`
- REST helpers: all use `rest_helper_with_auth()` for consistent auth header injection
- Extended message creation: `discord_create_message_ex` supports embeds and components JSON
- New REST helpers: guild, channel messages, reactions, interactions, DMs
- Gateway helpers: connect via binary channel, heartbeat with monotonic timestamp, identify with configurable intents and sharding
- Gateway state machine: DISCONNECTED → CONNECTING → CONNECTED → IDENTIFIED (with RESUMING path)
- Event translation: REST events → Discord events, Gateway WS frames → Discord events
- Gateway heartbeat management: caller-ticked via `discord_gateway_process_heartbeat(ctx, now_ms)`
- JSON field extraction: handles escaped quotes (`\"` and `\\`), unescapes on extraction

## Key Invariants

1. **No syscalls, no callbacks, no hidden I/O** — All networking lives in the caller.
2. **Event-driven** — Caller pulls events via `discord_next_event()`; library never pushes.
3. **Pure state machine** — Transitions driven only by input buffers and caller context.
4. **Plumbing only** — Library encodes/decodes Discord protocol; caller owns all policy and decisions.
5. **Strict warnings** — Compiles cleanly with `-Wall -Wextra -Wpedantic -Werror`.
6. **No direct HTTP/2 framing** — All HTTP/2 frame encoding/decoding delegated to librest/shaggy.
7. **Self-contained events** — All `discord_event_t` fields use embedded arrays, not pointers. Events are safe to copy and queue without lifetime concerns.
8. **Configurable intents** — Gateway identify payload uses caller-specified intents from `discord_config_t`, defaulting to all non-privileged intents.

## Data Flow

### REST API (e.g. Create Message)
```
discord_create_message(ctx, channel_id, content)
  → Builds Authorization header ("Bot {token}")
  → Calls rest_post_json("/channels/{channel_id}/messages", json_body, headers)
  → REST generates HTTP/2 HEADERS + DATA frames via HPACK
  → Output available via discord_get_output → rest_get_output
  → Event: DISCORD_EVENT_API_RESPONSE when response arrives (with status_code)
```

### Gateway (WebSocket Real-Time)
```
discord_gateway_connect(ctx)
  → Calls rest_establish_binary_channel(ctx->rest)
  → REST generates extended CONNECT HEADERS frame
  → Output available via discord_get_output

discord_gateway_send_identify(ctx)
  → Builds identify JSON payload with token, intents (from config), properties
  → If num_shards > 0, includes shard array
  → Calls rest_ws_send_text(ctx->rest, identify_json)
  → Gateway emits READY event when server responds

discord_gateway_send_heartbeat(ctx, last_sequence, now_ms)
  → Builds heartbeat JSON payload
  → Records last_heartbeat_ms = now_ms for timing
  → Calls rest_ws_send_text(ctx->rest, heartbeat_json)

Gateway events arrive via discord_feed_input → rest_feed_input:
  REST_EVENT_WS_TEXT → parse JSON → emit discord_event_t
  REST_EVENT_WS_BINARY → emit DISCORD_EVENT_RAW
  REST_EVENT_WS_CLOSE → emit DISCORD_EVENT_DISCONNECTED
```

### Event Translation
```
REST_EVENT_RESPONSE_HEADERS  → store pending status code
REST_EVENT_RESPONSE_DATA     → accumulate JSON body
REST_EVENT_RESPONSE_COMPLETE → emit DISCORD_EVENT_API_RESPONSE (with status_code + body)
REST_EVENT_WS_TEXT           → parse Gateway JSON → emit typed Discord event
REST_EVENT_WS_CLOSE          → DISCORD_EVENT_DISCONNECTED
REST_EVENT_BINARY_CHANNEL_READY → DISCORD_EVENT_CONNECTED
REST_EVENT_ERROR             → DISCORD_EVENT_ERROR
```

### Gateway Event Mapping
```
op 0, t="READY"                → DISCORD_EVENT_READY (session_id, user_id, gateway_url)
op 0, t="MESSAGE_CREATE"      → DISCORD_EVENT_MESSAGE_CREATE (id, channel_id, guild_id, author_id, content, timestamp)
op 0, t="MESSAGE_UPDATE"      → DISCORD_EVENT_MESSAGE_UPDATE
op 0, t="MESSAGE_DELETE"      → DISCORD_EVENT_MESSAGE_DELETE
op 0, t="CHANNEL_*"           → DISCORD_EVENT_CHANNEL_CREATE/UPDATE/DELETE
op 0, t="GUILD_CREATE"        → DISCORD_EVENT_GUILD_CREATE
op 0, t="GUILD_UPDATE"        → DISCORD_EVENT_GUILD_UPDATE
op 0, t="GUILD_DELETE"        → DISCORD_EVENT_GUILD_DELETE
op 0, t="GUILD_MEMBER_*"      → DISCORD_EVENT_GUILD_MEMBER_ADD/REMOVE/UPDATE
op 0, t="GUILD_ROLE_*"        → DISCORD_EVENT_GUILD_ROLE_CREATE/UPDATE/DELETE
op 0, t="MESSAGE_REACTION_*"  → DISCORD_EVENT_MESSAGE_REACTION_ADD/REMOVE
op 0, t="PRESENCE_UPDATE"     → DISCORD_EVENT_PRESENCE_UPDATE
op 0, t="TYPING_START"        → DISCORD_EVENT_TYPING_START
op 0, t="VOICE_STATE_UPDATE"  → DISCORD_EVENT_VOICE_STATE_UPDATE
op 0, t="THREAD_*"            → DISCORD_EVENT_THREAD_CREATE/UPDATE/DELETE
op 0, t="INTERACTION_CREATE"  → DISCORD_EVENT_INTERACTION_CREATE (id, type, token, application_id, data_json)
op 1  (server heartbeat)      → DISCORD_EVENT_HEARTBEAT_REQUEST
op 7  (reconnect)             → DISCORD_EVENT_RECONNECT_REQUEST
op 9  (invalid session)       → clears session_id, state → DISCONNECTED
op 10 (hello)                 → extracts heartbeat_interval, state → CONNECTED
op 11 (heartbeat ACK)         → DISCORD_EVENT_HEARTBEAT_ACK
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
- `discord_gateway_process_heartbeat(ctx, now_ms)` compares caller-supplied timestamp against last heartbeat send time
- Returns 1 if `(now_ms - last_heartbeat_ms) >= heartbeat_interval_ms` or if no heartbeat has been sent yet
- Caller invokes this periodically with a monotonic timestamp (pure function, no timers, no threads)
- Heartbeat payload includes last sequence number for resume support
- When server sends op 1 (heartbeat request), DISCORD_EVENT_HEARTBEAT_REQUEST is emitted for immediate caller response
- When server sends op 11 (heartbeat ACK), DISCORD_EVENT_HEARTBEAT_ACK is emitted for connection health tracking

## Event Queue Behavior

- Ring buffer with configurable size (`discord_config_t.event_queue_size`)
- When full: `enqueue_event` returns -1, new events are **dropped** (oldest events preserved)
- Events use embedded arrays (no heap allocation per event, no pointer lifetime issues)
- `discord_event_t` is safe to copy — all data is self-contained in the struct

## Deliberate Absences

- **No JSON parser** — Library emits raw JSON strings; caller owns parsing
- **No event cache** — No in-memory cache of guilds, channels, or members
- **No rate limit auto-retry** — Library emits `DISCORD_EVENT_RATE_LIMITED` on HTTP 429 with parsed headers (retry_after_ms, is_global, bucket); caller implements retry logic
- **No voice support** — Voice is a separate protocol; out of scope
- **No shard management** — Caller manages sharding strategy (library passes shard config to identify)
- **No OAuth2** — Bot token auth only

## Testing Strategy

- **Smoke test** (`test_discord_smoke.c`): create/destroy lifecycle, NULL handling, config validation, event queue drain, API helper NULL checks, gateway state accessor, reset, set_token, event struct copy safety
- **Dialectic test** (`test_discord_dialectic.c`): paired client↔server exchange simulating Discord API round-trip and Gateway handshake, multiple REST requests, new helper frame production, embedded event field verification
- **Gateway lifecycle test** (`test_discord_gateway_lifecycle.c`): heartbeat timing logic, identify intents, gateway state transitions, event type completeness (35 types), interaction event fields
- **Fuzz harness** (`fuzz_discord.c`): 3 targets (API surface including new helpers, event translation, config fuzzing with intents/shards)
- **Valgrind integration**: all tests run under Valgrind with `--leak-check=full --error-exitcode=1` via `cmake -DENABLE_VALGRIND=ON`