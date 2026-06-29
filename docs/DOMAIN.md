# DOMAIN.md — libdiscord

## Core Concepts
- **Discord REST API v10**: HTTP/2 REST calls to `https://discord.com/api/v10/*` endpoints. Authenticated via `Bot {token}` header.
- **Discord Gateway**: WebSocket connection for real-time events (uses librest binary channel / RFC 8441 extended CONNECT).
- **Events**: Structured `discord_event_t` events emitted by the library. All event fields use embedded arrays (copy-safe, no pointer lifetime issues). Caller owns all JSON parsing and policy decisions.
- **Bot Token**: Discord bot authentication token. Library stores it and injects `Authorization: Bot ***` headers. Can be updated post-creation via `discord_set_token()`.
- **Intents**: Bitfield indicating which Gateway events the bot wants to receive. Configurable via `discord_config_t.intents` (0 = all non-privileged intents via `DISCORD_INTENTS_DEFAULT`). Sent in the identify payload.
- **Sharding**: Gateway sharding via `discord_config_t.shard_id` and `num_shards`. When `num_shards > 0`, the identify payload includes the shard array.
- **Embeds**: Rich message formatting (title, description, fields, footer, etc.) — supported via `discord_create_message_ex()` with raw embeds JSON.
- **Components**: Message components (buttons, select menus) — supported via `discord_create_message_ex()` with raw components JSON.
- **Interactions**: Slash commands, buttons, select menus, modals — `DISCORD_EVENT_INTERACTION_CREATE` extracts id, type, token, application_id, and raw data_json.
- **Heartbeats**: Gateway requires periodic heartbeats to maintain connection. Library provides `discord_gateway_process_heartbeat(ctx, now_ms)` — caller supplies monotonic timestamp; library compares against last heartbeat send time.
- **Sequence Numbers**: Gateway sends sequence numbers with each dispatch event. Library tracks them for resume support.

## REST API Workflows
- **Create Message**: `discord_create_message(ctx, channel_id, content)` → POST /channels/{id}/messages
- **Create Message (extended)**: `discord_create_message_ex(ctx, channel_id, content, embeds_json, components_json)` → POST with embeds/components
- **Edit Message**: `discord_edit_message(ctx, channel_id, message_id, content)` → PATCH /channels/{id}/messages/{msg_id}
- **Delete Message**: `discord_delete_message(ctx, channel_id, message_id)` → DELETE /channels/{id}/messages/{msg_id}
- **Get Channel**: `discord_get_channel(ctx, channel_id)` → GET /channels/{id}
- **Get Guild**: `discord_get_guild(ctx, guild_id)` → GET /guilds/{id}
- **Get Channel Messages**: `discord_get_channel_messages(ctx, channel_id)` → GET /channels/{id}/messages
- **Add Reaction**: `discord_add_reaction(ctx, channel_id, message_id, emoji)` → PUT /channels/{cid}/messages/{mid}/reactions/{emoji}/@me
- **Delete Own Reaction**: `discord_delete_own_reaction(ctx, channel_id, message_id, emoji)` → DELETE
- **Create Interaction Response**: `discord_create_interaction_response(ctx, id, token, json)` → POST /interactions/{id}/{token}/callback
- **Create DM**: `discord_create_dm(ctx, recipient_id)` → POST /users/@me/channels
- All helpers inject auth headers automatically when bot_token is configured.
- API responses arrive as `DISCORD_EVENT_API_RESPONSE` with embedded json_body buffer (copy-safe, self-contained).

## Gateway Workflows
1. `discord_gateway_connect(ctx)` → establishes WebSocket via extended CONNECT
2. Server sends Hello (op 10) with heartbeat_interval → `DISCORD_EVENT_HEARTBEAT_REQUEST` is NOT emitted (Hello is internal)
3. `discord_gateway_send_identify(ctx)` → sends identify payload (op 2) with token + intents + optional shard array
4. Server sends READY (op 0, t="READY") with session_id, user info → `DISCORD_EVENT_READY`
5. `discord_gateway_process_heartbeat(ctx, now_ms)` → returns 1 when heartbeat interval elapsed
6. `discord_gateway_send_heartbeat(ctx, last_sequence, now_ms)` → sends heartbeat (op 1)
7. Server sends Heartbeat ACK (op 11) → `DISCORD_EVENT_HEARTBEAT_ACK`
8. Server may send op 1 (request heartbeat) → `DISCORD_EVENT_HEARTBEAT_REQUEST`
9. Server may send op 7 (reconnect) → `DISCORD_EVENT_RECONNECT_REQUEST`
10. On disconnect: `discord_gateway_send_resume(ctx)` → sends resume (op 6)
11. `discord_gateway_state(ctx)` → returns current connection state enum
12. `discord_reset(ctx)` → clears all internal state for reuse

## Gateway Opcodes
| Op | Name | Description |
|----|------|-------------|
| 0  | Dispatch | Server dispatching an event (see event mapping below) |
| 1  | Heartbeat | Server requests client heartbeat → `DISCORD_EVENT_HEARTBEAT_REQUEST` |
| 2  | Identify | Client identifying to gateway |
| 6  | Resume | Client requesting session resume |
| 7  | Reconnect | Server requests client reconnect → `DISCORD_EVENT_RECONNECT_REQUEST` |
| 9  | Invalid Session | Session invalidated → clears session_id |
| 10 | Hello | Gateway hello with heartbeat_interval (internal) |
| 11 | Heartbeat ACK | Server acknowledging heartbeat → `DISCORD_EVENT_HEARTBEAT_ACK` |

## Discord Event Mapping
| Gateway Event | discord_event_type_t |
|---------------|---------------------|
| READY | DISCORD_EVENT_READY |
| MESSAGE_CREATE | DISCORD_EVENT_MESSAGE_CREATE |
| MESSAGE_UPDATE | DISCORD_EVENT_MESSAGE_UPDATE |
| MESSAGE_DELETE | DISCORD_EVENT_MESSAGE_DELETE |
| CHANNEL_CREATE | DISCORD_EVENT_CHANNEL_CREATE |
| CHANNEL_UPDATE | DISCORD_EVENT_CHANNEL_UPDATE |
| CHANNEL_DELETE | DISCORD_EVENT_CHANNEL_DELETE |
| GUILD_CREATE | DISCORD_EVENT_GUILD_CREATE |
| GUILD_UPDATE | DISCORD_EVENT_GUILD_UPDATE |
| GUILD_DELETE | DISCORD_EVENT_GUILD_DELETE |
| GUILD_MEMBER_ADD | DISCORD_EVENT_GUILD_MEMBER_ADD |
| GUILD_MEMBER_REMOVE | DISCORD_EVENT_GUILD_MEMBER_REMOVE |
| GUILD_MEMBER_UPDATE | DISCORD_EVENT_GUILD_MEMBER_UPDATE |
| GUILD_ROLE_CREATE | DISCORD_EVENT_GUILD_ROLE_CREATE |
| GUILD_ROLE_UPDATE | DISCORD_EVENT_GUILD_ROLE_UPDATE |
| GUILD_ROLE_DELETE | DISCORD_EVENT_GUILD_ROLE_DELETE |
| MESSAGE_REACTION_ADD | DISCORD_EVENT_MESSAGE_REACTION_ADD |
| MESSAGE_REACTION_REMOVE | DISCORD_EVENT_MESSAGE_REACTION_REMOVE |
| PRESENCE_UPDATE | DISCORD_EVENT_PRESENCE_UPDATE |
| TYPING_START | DISCORD_EVENT_TYPING_START |
| VOICE_STATE_UPDATE | DISCORD_EVENT_VOICE_STATE_UPDATE |
| THREAD_CREATE | DISCORD_EVENT_THREAD_CREATE |
| THREAD_UPDATE | DISCORD_EVENT_THREAD_UPDATE |
| THREAD_DELETE | DISCORD_EVENT_THREAD_DELETE |
| INTERACTION_CREATE | DISCORD_EVENT_INTERACTION_CREATE |

## REST Event Mapping
| REST Event | discord_event_type_t |
|------------|---------------------|
| REST_EVENT_RESPONSE_HEADERS | (store pending status code) |
| REST_EVENT_RESPONSE_DATA | (accumulate body) |
| REST_EVENT_RESPONSE_COMPLETE | DISCORD_EVENT_API_RESPONSE (with status_code + body) |
| REST_EVENT_BINARY_CHANNEL_READY | DISCORD_EVENT_CONNECTED |
| REST_EVENT_WS_TEXT | (parse JSON → typed Discord event) |
| REST_EVENT_WS_BINARY | DISCORD_EVENT_RAW |
| REST_EVENT_WS_CLOSE | DISCORD_EVENT_DISCONNECTED |
| REST_EVENT_ERROR | DISCORD_EVENT_ERROR |

## Deliberate Absences
- **No JSON parser** — Library emits raw JSON strings; caller owns parsing.
- **No event cache** — No in-memory cache of guilds, channels, or members.
- **Rate limiting** — Library emits `DISCORD_EVENT_RATE_LIMITED` on HTTP 429 with parsed headers (`retry_after_ms`, `is_global`, `bucket`). Caller implements retry logic.
- **No voice support** — Voice uses a separate protocol; out of scope.
- **No shard management** — Caller manages sharding strategy (library passes config to identify).
- **No OAuth2** — Bot token auth only.