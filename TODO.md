# libdiscord – Implementation TODO

## Phase 1: Bootstrap (Current)
- [x] Repository scaffold: CMake, headers, source skeleton, tests, docs, ADRs
- [x] Core context lifecycle (create/destroy with librest integration)
- [x] Event queue (ring buffer with configurable size)
- [x] Input/output delegation to librest
- [x] NULL handling and error paths

## Phase 2: REST API Helpers
- [x] `discord_create_message` — POST /channels/{id}/messages
- [x] `discord_edit_message` — PUT /channels/{id}/messages/{msg_id}
- [x] `discord_delete_message` — DELETE /channels/{id}/messages/{msg_id}
- [x] `discord_get_channel` — GET /channels/{id}
- [x] Bot token authentication header injection
- [x] API response event translation

## Phase 3: Gateway Support
- [x] `discord_gateway_connect` — binary channel establishment
- [x] `discord_gateway_send_identify` — identify payload with token + intents
- [x] `discord_gateway_send_heartbeat` — heartbeat with sequence number
- [x] `discord_gateway_send_resume` — resume payload
- [x] Gateway event translation (WS text → Discord events)
- [x] Heartbeat interval tracking

## Phase 4: Testing & Verification
- [x] Smoke test: lifecycle, NULL handling, API helpers
- [x] Dialectic test: REST round-trip, Gateway handshake simulation
- [x] Fuzz harness: API surface, event translation, config fuzzing
- [x] Valgrind-clean (zero leaks, zero errors)

## Future Work
- [ ] Embed builder helpers (rich message formatting)
- [ ] Interaction response helpers (slash commands)
- [ ] Voice channel support
- [ ] Rate limit awareness
- [ ] Sharding support
