# libdiscord — Implementation TODO

Generated from full codebase audit. Items grouped by priority tier.

---

## Tier 1: Bugs & Correctness — DONE

- [x] **Event queue string lifetime — fixed with embedded arrays**
  All event struct fields now use `char[]` arrays instead of `const char *` pointers.
  Events are self-contained, copy-safe, no lifetime issues.

- [x] **API response status_code — now populated**
  `translate_rest_event()` stores `pending_status_code` from RESPONSE_HEADERS.
  `emit_api_response()` copies it into the event.

- [x] **API response body — copied into event buffer**
  `emit_api_response()` now copies body into `discord_api_response_t.json_body[8192]`
  with `truncated` flag. No more shared buffer aliasing.

- [x] **JSON string extraction handles escapes**
  `json_find_string()` now skips `\"` and `\\` escapes in the inner scan loop.

- [x] **Intents configurable via config struct**
  `discord_config_t.intents` field added. `DISCORD_INTENTS_DEFAULT` macro provides
  all non-privileged intents. Identify payload uses `config.intents` (0 = default).

---

## Tier 2: Incomplete Implementation — DONE

- [x] **discord_gateway_process_heartbeat() — real timing**
  Now accepts `uint64_t now_ms` and compares against `last_heartbeat_ms`.
  Returns 1 if `(now_ms - last_heartbeat_ms) >= heartbeat_interval_ms` or no heartbeat sent yet.

- [x] **Dialectic test — embedded field verification**
  New `test_embedded_event_fields()` verifies event structs use embedded arrays.
  New `test_new_rest_helpers_produce_frames()` tests new REST helpers produce HEADERS frames.

- [x] **Gateway op 1 (server heartbeat) — emits DISCORD_EVENT_HEARTBEAT_REQUEST**

- [x] **Gateway op 7 (reconnect) — emits DISCORD_EVENT_RECONNECT_REQUEST**

- [x] **Gateway op 11 (heartbeat ACK) — emits DISCORD_EVENT_HEARTBEAT_ACK**

---

## Tier 3: New Discord Event Types — DONE

All 19 new event types implemented in `discord.h` and `translate_ws_text()`:

- [x] CHANNEL_CREATE, CHANNEL_UPDATE, CHANNEL_DELETE
- [x] GUILD_UPDATE, GUILD_DELETE
- [x] GUILD_MEMBER_ADD, GUILD_MEMBER_REMOVE, GUILD_MEMBER_UPDATE
- [x] GUILD_ROLE_CREATE, GUILD_ROLE_UPDATE, GUILD_ROLE_DELETE
- [x] MESSAGE_REACTION_ADD, MESSAGE_REACTION_REMOVE
- [x] PRESENCE_UPDATE, TYPING_START, VOICE_STATE_UPDATE
- [x] THREAD_CREATE, THREAD_UPDATE, THREAD_DELETE
- [x] INTERACTION_CREATE with `discord_interaction_t` payload (id, type, token, application_id, data_json)

---

## Tier 4: New REST API Helpers — DONE

- [x] **discord_get_guild(ctx, guild_id)** — GET /guilds/{guild_id}
- [x] **discord_get_channel_messages(ctx, channel_id)** — GET /channels/{id}/messages
- [x] **discord_add_reaction(ctx, channel_id, message_id, emoji)** — PUT .../reactions/{emoji}/@me
- [x] **discord_delete_own_reaction(ctx, channel_id, message_id, emoji)** — DELETE
- [x] **discord_create_interaction_response(ctx, id, token, json)** — POST /interactions/{id}/{token}/callback
- [x] **discord_create_dm(ctx, recipient_id)** — POST /users/@me/channels
- [x] **discord_create_message_ex()** — extended version with embeds/components JSON

---

## Tier 5: API Design & Config — DONE

- [x] **discord_config_t.intents** — configurable intents bitfield
- [x] **discord_config_t.shard_id / num_shards** — sharding support in identify
- [x] **discord_gateway_state(ctx)** — public state accessor
- [x] **discord_set_token(ctx, token)** — post-creation token rotation
- [x] **discord_reset(ctx)** — reset internal state for reuse
- [x] **discord_interaction_t** — interaction event payload struct
- [x] **discord_gateway_state_t** — public gateway state enum

---

## Tier 6: Testing — DONE

- [x] **Valgrind CTest integration** — `cmake -DENABLE_VALGRIND=ON` runs all tests under Valgrind
- [x] **Gateway lifecycle test** — heartbeat timing, intents, state transitions, 34 event types, interaction fields
- [x] **Dialectic test: embedded field verification** — event struct copy safety, new REST helpers
- [x] **Smoke test: new API coverage** — gateway state, reset, set_token, event struct copy safety

---

## Tier 7: Documentation — DONE

- [x] **ARCHITECTURE.md** — updated with new event types, REST helpers, heartbeat timing, config, gateway state
- [x] **DOMAIN.md** — updated with intents, sharding, components, interactions, heartbeat timing
- [x] **README.md** — updated with new features, removed stale embed claim
- [x] **docs/README.md** — updated with new test, Valgrind integration, ADR 005 note
- [x] **CHANGELOG.md** — created with v0.2.0 changes
- [x] **ADR 005 noted as unassigned** in docs/README.md

---

## Remaining: Future Work

- [x] **Manpages for Gateway functions** — discord_gateway_connect.3, discord_gateway_send_heartbeat.3, discord_gateway_send_identify.3, discord_gateway_send_resume.3, discord_gateway_process_heartbeat.3, discord_gateway_state.3
- [x] **Manpage for discord_types.h** — permission flags, intent bitfield, channel types, message types
- [x] **Rate limit header extraction** — Parse X-RateLimit-Remaining, X-RateLimit-Reset-After, Retry-After from API response headers
- [x] **HTTP 429 (rate limited) event type** — DISCORD_EVENT_RATE_LIMITED with retry_after_ms
- [ ] **Voice channel support** — Voice uses a separate protocol; out of scope but voice state events are now tracked
- [ ] **Webhook support** — Incoming webhook payload parsing for DISCORD_ROLE_SERVER
- [ ] **OAuth2 bearer token support** — Beyond bot tokens (low priority)
- [ ] **Voice connection protocol** — Separate WebSocket + UDP; would need its own module