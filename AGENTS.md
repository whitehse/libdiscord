# AGENTS.md — libdiscord

**Project identity**: Pure C state-machine Discord bot support library built on top of librest and shaggy (libhttp2). System-call free, callback free. All I/O, networking, and event handling lives exclusively in the calling application. The library only consumes/produces byte buffers and explicit state transitions. It provides convenient Discord REST API v10 helpers (create/edit/delete message, get channel, get guild, reactions, interactions, DMs) and Gateway support (WebSocket via librest binary channel) with heartbeat/identify handshake, configurable intents, and sharding.

**Key commands** (run from repo root):
- `cmake -B build -S . && cmake --build build` — configure and build the static library + tests
- `ctest --test-dir build` — run verification tests (3 test suites)
- `cmake -B build -S . -DENABLE_VALGRIND=ON && cmake --build build && ctest --test-dir build -R valgrind` — Valgrind leak tests
- `cmake --build build --target install` — install (optional)

**Documentation map** (progressive disclosure):
- AGENTS.md (this file) — start here for every task
- ARCHITECTURE.md — module boundaries, invariants, plumbing philosophy (ADR 006), event model, librest integration
- docs/README.md — full documentation index
- docs/DOMAIN.md — Discord domain glossary: REST API, Gateway, events, intents, embeds, interactions
- docs/decisions/ — Architecture Decision Records (ADRs)
- CHANGELOG.md — version history

**Operating rules**:
- Never introduce system calls, callbacks, or hidden I/O inside the library.
- State machine design follows libassh patterns: explicit states, deterministic transitions driven only by input buffers and caller-supplied context.
- Every change must keep the library buildable with `-Wall -Wextra -Wpedantic -Werror` (or MSVC equivalent) and pass existing tests.
- Prefer small, reviewable patches. Update relevant docs/ADRs when architecture or domain assumptions change.
- Hermes agent (or any coding agent) must consult AGENTS.md before editing code or docs.
- This library sits strictly on top of librest; do not modify librest or shaggy sources. Use their public APIs for transport.
- Core remains pure plumbing (ADR 006): emit structured events with raw JSON; caller owns all parsing, policy, and decisions.
- Support configurable initialization via `discord_config_t` (including event_queue_size, bot_token, gateway_url, api_base_url, intents, shard_id, num_shards).
- Dialectic (client+server) development and testing is mandatory.
- Event structs must be self-contained (embedded arrays, no pointers) — safe to copy and queue without lifetime concerns.

**Definition of done** (for any ticket):
- Code compiles cleanly under strict warnings.
- Tests pass (`ctest`).
- Valgrind tests pass (`ctest -R valgrind`).
- AGENTS.md, ARCHITECTURE.md, and relevant docs remain accurate.
- No new syscalls or callbacks introduced.
- State machine remains pure (inputs → state/output only).
- Any new feature exercised via dialectic buffer-exchange test.

**Dependencies**:
- librest located at /home/dwhite/librest — used for HTTP/2 REST operations and WebSocket binary channel support
- shaggy (libhttp2) located at /home/dwhite/shaggy — underlying HTTP/2 transport (linked transitively via librest)
- Link and include paths configured in CMake.

**Current Interface Direction (ADR 009 / ADR 010)**:
- All protocol modules expose a consistent shape:
  - `discord_config_t` (with `event_queue_size`, `bot_token`, `gateway_url`, `api_base_url`, `intents`, `shard_id`, `num_shards`)
  - `discord_create(role)` and `discord_create_with_config(role, config)`
  - `discord_feed_input(ctx, data, len)`
  - `discord_next_event(ctx, &event)` returning 1 if event available
  - `discord_get_output(ctx, buf, max)`
  - `discord_set_token(ctx, token)` — post-creation token rotation
  - `discord_reset(ctx)` — reset state for reuse
  - `discord_gateway_state(ctx)` — query gateway connection state
- Discord REST helpers: `discord_create_message`, `discord_create_message_ex`, `discord_edit_message`, `discord_delete_message`, `discord_get_channel`, `discord_get_guild`, `discord_get_channel_messages`, `discord_add_reaction`, `discord_delete_own_reaction`, `discord_create_interaction_response`, `discord_create_dm`
- Gateway helpers: `discord_gateway_connect`, `discord_gateway_send_heartbeat`, `discord_gateway_send_identify`, `discord_gateway_send_resume`, `discord_gateway_process_heartbeat`, `discord_gateway_state`
- 35 event types covering lifecycle, messages, channels, guilds, members, roles, reactions, presence, typing, voice, threads, interactions, API responses, and rate limiting
- All public interfaces follow opaque type principles from Hanson's *C Interfaces and Implementations*.

**Testing, Fuzzing & Valgrind Policy** (see ADR 003)
- Every change to `src/discord.c` or `include/discord.h` must add or update tests in `tests/`.
- Run `ctest` before considering any change complete.
- Run `ctest -R valgrind` for leak verification.
- All tests must pass under Valgrind with no leaks or memory errors.