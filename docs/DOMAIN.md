# DOMAIN.md — libdiscord

## Core Concepts
- **Discord REST API v10**: HTTP/2 REST calls to `https://discord.com/api/v10/*` endpoints. Authenticated via `Bot {token}` header.
- **Discord Gateway**: WebSocket connection for real-time events (uses librest binary channel / RFC 8441 extended CONNECT).
- **Events**: Structured `discord_event_t` events emitted by the library. Caller owns all JSON parsing and policy decisions.
- **Bot Token**: Discord bot authentication token. Library stores it and injects `Authorization: Bot {token}` headers.
- **Intents**: Bitfield indicating which Gateway events the bot wants to receive. Sent in the identify payload.
- **Embeds**: Rich message formatting (title, description, fields, footer, etc.) — future helper support.
- **Interactions**: Slash commands, buttons, select menus — future helper support.
- **Heartbeats**: Gateway requires periodic heartbeats to maintain connection. Library provides helpers; caller owns timing.
- **Sequence Numbers**: Gateway sends sequence numbers with each dispatch event. Library tracks them for resume support.

## REST API Workflows
- **Create Message**: `discord_create_message(ctx, channel_id, content)` → POST /channels/{id}/messages
- **Edit Message**: `discord_edit_message(ctx, channel_id, message_id, content)` → PUT /channels/{id}/messages/{msg_id}
- **Delete Message**: `discord_delete_message(ctx, channel_id, message_id)` → DELETE /channels/{id}/messages/{msg_id}
- **Get Channel**: `discord_get_channel(ctx, channel_id)` → GET /channels/{id}
- All helpers inject auth headers automatically when bot_token is configured.
- API responses arrive as `DISCORD_EVENT_API_RESPONSE` with raw JSON body.

## Gateway Workflows
1. `discord_gateway_connect(ctx)` → establishes WebSocket via extended CONNECT
2. Server sends Hello (op 10) with heartbeat_interval
3. `discord_gateway_send_identify(ctx)` → sends identify payload (op 2) with token + intents
4. Server sends READY (op 0, t="READY") with session_id, user info
5. `discord_gateway_send_heartbeat(ctx, last_sequence)` → sends heartbeat (op 1)
6. Server sends Heartbeat ACK (op 11)
7. On disconnect: `discord_gateway_send_resume(ctx)` → sends resume (op 6)

## Gateway Opcodes
| Op | Name | Description |
|----|------|-------------|
| 0  | Dispatch | Server dispatching an event |
| 1  | Heartbeat | Client/server heartbeat |
| 2  | Identify | Client identifying to gateway |
| 6  | Resume | Client requesting session resume |
| 7  | Reconnect | Server requesting client reconnect |
| 9  | Invalid Session | Session invalidated |
| 10 | Hello | Gateway hello with heartbeat interval |
| 11 | Heartbeat ACK | Server acknowledging heartbeat |

## Discord Event Mapping
| Gateway Event | discord_event_type_t |
|---------------|---------------------|
| READY | DISCORD_EVENT_READY |
| MESSAGE_CREATE | DISCORD_EVENT_MESSAGE_CREATE |
| MESSAGE_UPDATE | DISCORD_EVENT_MESSAGE_UPDATE |
| MESSAGE_DELETE | DISCORD_EVENT_MESSAGE_DELETE |
| GUILD_CREATE | DISCORD_EVENT_GUILD_CREATE |
| INTERACTION_CREATE | DISCORD_EVENT_INTERACTION_CREATE |

## REST Event Mapping
| REST Event | discord_event_type_t |
|------------|---------------------|
| REST_EVENT_RESPONSE_HEADERS | (accumulate body) |
| REST_EVENT_RESPONSE_DATA | (accumulate body) |
| REST_EVENT_RESPONSE_COMPLETE | DISCORD_EVENT_API_RESPONSE |
| REST_EVENT_BINARY_CHANNEL_READY | DISCORD_EVENT_CONNECTED |
| REST_EVENT_WS_TEXT | (parse JSON → typed Discord event) |
| REST_EVENT_WS_BINARY | DISCORD_EVENT_RAW |
| REST_EVENT_WS_CLOSE | DISCORD_EVENT_DISCONNECTED |
| REST_EVENT_ERROR | DISCORD_EVENT_ERROR |

## Deliberate Absences
- **No JSON parser** — Library emits raw JSON strings; caller owns parsing.
- **No event cache** — No in-memory cache of guilds, channels, or members.
- **No rate limiting** — Caller implements rate limit handling (HTTP 429 responses).
- **No voice support** — Voice uses a separate protocol; out of scope.
- **No shard management** — Caller manages sharding strategy.
- **No OAuth2** — Bot token auth only.
