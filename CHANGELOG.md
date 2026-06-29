# Changelog

All notable changes to libdiscord will be documented in this file.

## [0.2.0] — 2026-06-29

### Added — Tier 8 Rate Limit Support
- `DISCORD_EVENT_RATE_LIMITED` event type — emitted when HTTP 429 is received
- `discord_rate_limit_t` payload struct with `retry_after_ms`, `is_global`, and `bucket` fields
- Rate limit header extraction from REST responses: `Retry-After`, `X-RateLimit-Remaining`, `X-RateLimit-Reset-After`, `X-RateLimit-Bucket`, `X-RateLimit-Global`, `X-RateLimit-Scope`
- Both `DISCORD_EVENT_API_RESPONSE` and `DISCORD_EVENT_RATE_LIMITED` are emitted on 429 responses (API response for raw access, rate limit for structured convenience)

### Added — Tier 9 Manpages (7 new manpages)
- `discord_gateway_connect.3` — Gateway WebSocket connection
- `discord_gateway_send_identify.3` — identify payload with intents and sharding
- `discord_gateway_send_heartbeat.3` — heartbeat with sequence and timestamp
- `discord_gateway_send_resume.3` — session resume after disconnect
- `discord_gateway_process_heartbeat.3` — heartbeat timing check
- `discord_gateway_state.3` — gateway state accessor
- `discord_types.3` — permission flags, intent bitfields, channel types, message types

### Added — Tier 1 Bug Fixes
- Event struct fields changed from pointers to embedded arrays (copy-safe, no lifetime issues)
  - `discord_message_t`, `discord_ready_t`, `discord_api_response_t` now use `char[]` fields
  - Eliminated static buffer aliasing bug in `translate_ws_text()`
- `discord_api_response_t.status_code` now populated from `REST_EVENT_RESPONSE_HEADERS`
- API response body copied into event's embedded buffer (eliminates pointer aliasing across queued events)
- `json_find_string()` now handles `\"` and `\\` escapes in JSON string values
- Gateway intents made configurable via `discord_config_t.intents` (was hardcoded to 32767)
- `DISCORD_INTENTS_DEFAULT` macro provides all non-privileged intents

### Added — Tier 2 Stub Completion
- `discord_gateway_process_heartbeat(ctx, now_ms)` now accepts monotonic timestamp and performs real interval comparison (was stub that always returned 1)
- `discord_gateway_send_heartbeat(ctx, seq, now_ms)` records send timestamp internally
- Gateway op 1 (server heartbeat request) now emits `DISCORD_EVENT_HEARTBEAT_REQUEST`
- Gateway op 7 (reconnect) now emits `DISCORD_EVENT_RECONNECT_REQUEST`
- Gateway op 11 (heartbeat ACK) now emits `DISCORD_EVENT_HEARTBEAT_ACK`

### Added — Tier 3 New Event Types (19 new events)
- Channel events: `DISCORD_EVENT_CHANNEL_CREATE`, `CHANNEL_UPDATE`, `CHANNEL_DELETE`
- Guild events: `DISCORD_EVENT_GUILD_UPDATE`, `GUILD_DELETE`
- Member events: `DISCORD_EVENT_GUILD_MEMBER_ADD`, `MEMBER_REMOVE`, `MEMBER_UPDATE`
- Role events: `DISCORD_EVENT_GUILD_ROLE_CREATE`, `ROLE_UPDATE`, `ROLE_DELETE`
- Reaction events: `DISCORD_EVENT_MESSAGE_REACTION_ADD`, `REACTION_REMOVE`
- Presence/typing/voice: `DISCORD_EVENT_PRESENCE_UPDATE`, `TYPING_START`, `VOICE_STATE_UPDATE`
- Thread events: `DISCORD_EVENT_THREAD_CREATE`, `THREAD_UPDATE`, `THREAD_DELETE`
- Lifecycle: `DISCORD_EVENT_HEARTBEAT_REQUEST`, `HEARTBEAT_ACK`, `RECONNECT_REQUEST`

### Added — Tier 4 New REST API Helpers
- `discord_get_guild(ctx, guild_id)` — GET /guilds/{guild_id}
- `discord_get_channel_messages(ctx, channel_id)` — GET /channels/{id}/messages
- `discord_add_reaction(ctx, channel_id, message_id, emoji)` — PUT .../reactions/{emoji}/@me
- `discord_delete_own_reaction(ctx, channel_id, message_id, emoji)` — DELETE .../reactions/{emoji}/@me
- `discord_create_interaction_response(ctx, id, token, json)` — POST /interactions/{id}/{token}/callback
- `discord_create_dm(ctx, recipient_id)` — POST /users/@me/channels
- `discord_create_message_ex(ctx, channel_id, content, embeds_json, components_json)` — extended message creation with embeds and components support

### Added — Tier 5 API Design Improvements
- `discord_config_t.intents` — configurable Gateway intents bitfield (0 = all non-privileged)
- `discord_config_t.shard_id` / `num_shards` — Gateway sharding support in identify payload
- `discord_gateway_state(ctx)` — public gateway state accessor (returns `discord_gateway_state_t` enum)
- `discord_set_token(ctx, token)` — update bot token post-creation (e.g. for token rotation)
- `discord_reset(ctx)` — reset internal state for reuse without destroying context
- `discord_interaction_t` payload struct for `INTERACTION_CREATE` events (id, type, token, application_id, data_json)
- `discord_gateway_state_t` public enum (DISCONNECTED, CONNECTING, CONNECTED, IDENTIFIED, RESUMING)

### Added — Tier 6 Testing
- Gateway lifecycle test (`tests/test_discord_gateway_lifecycle.c`): heartbeat timing, intents, state transitions, event type completeness, interaction fields
- Valgrind CTest integration via `cmake -DENABLE_VALGRIND=ON`
- Dialectic test: new REST helper frame production tests, embedded event field verification
- Smoke test: gateway state accessor, reset, set_token, event struct copy safety tests
- Fuzz harness: expanded to cover new REST helpers, reset, gateway state

### Changed
- `discord_gateway_send_heartbeat()` signature: added `uint64_t now_ms` parameter
- `discord_gateway_process_heartbeat()` signature: added `uint64_t now_ms` parameter
- `discord_event_t.error_msg` changed from `const char *` to `char[256]` (embedded, copy-safe)
- `discord_event_t.data.message` fields changed from `const char *` to `char[]` arrays
- `discord_event_t.data.ready` fields changed from `const char *` to `char[]` arrays
- `discord_event_t.data.api_response.json_body` changed from `const char *` to `char[8192]` with `truncated` flag
- `discord.h` now includes `discord_types.h` for intent bitfield constants

### Fixed
- Static buffer aliasing in MESSAGE_CREATE event extraction (was shared across all events)
- API response `status_code` never populated (always returned 0)
- API response `json_body` pointer invalidation when multiple responses queued
- JSON string extraction fails on escaped quotes (`\"`)
- Gateway identify payload hardcoded intents to 32767 (all intents including privileged)

## [0.1.0] — 2026-06-29

### Added
- Initial release: core context lifecycle, event queue, REST helpers, Gateway support
- See original TODO.md Phase 1-4 for complete initial feature set