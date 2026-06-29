# libdiscord Documentation

This library provides a pure-C, plumbing-style Discord bot support library on top of librest and shaggy (libhttp2).

## Entry Points

- [AGENTS.md](../AGENTS.md) (root) — Start here: build, test, agent rules, definition of done.
- [ARCHITECTURE.md](../ARCHITECTURE.md) — Module boundaries, invariants, plumbing philosophy (ADR 006), event model, librest integration.
- [DOMAIN.md](DOMAIN.md) — Discord domain glossary: REST API, Gateway, events, intents, embeds, interactions.
- [CHANGELOG.md](../CHANGELOG.md) — Version history and notable changes.

## Architecture Decision Records

- [001-agent-ready-documentation.md](decisions/001-agent-ready-documentation.md) — Agent-ready documentation scaffold
- [002-event-loop-compatibility.md](decisions/002-event-loop-compatibility.md) — Event loop compatibility requirements
- [003-testing-fuzzing-valgrind.md](decisions/003-testing-fuzzing-valgrind.md) — Testing, fuzzing, and Valgrind policy
- [004-dialectic-client-server-testing.md](decisions/004-dialectic-client-server-testing.md) — Paired client/server buffer-exchange testing
- **ADR 005**: Not assigned (number skipped between 004 and 006)
- [006-core-library-as-plumbing-pdu-stack.md](decisions/006-core-library-as-plumbing-pdu-stack.md) — Core as thin PDU parser (ADR 006)
- [007-documentation-and-manpage-updates.md](decisions/007-documentation-and-manpage-updates.md) — Documentation and manpages updated with API changes
- [008-c-only-examples-and-codebase.md](decisions/008-c-only-examples-and-codebase.md) — Strict C language requirement (no C++)
- [009-consistent-protocol-interfaces.md](decisions/009-consistent-protocol-interfaces.md) — Unified `*_config_t` + `*_next_event` interface
- [010-c-interfaces-and-language-bindings.md](decisions/010-c-interfaces-and-language-bindings.md) — Hanson's *C Interfaces and Implementations* principles + FFI-friendly design

## API Documentation

- Manpages (section 3): See `man/man3/` (or installed under ${CMAKE_INSTALL_MANDIR}/man3).
  - discord_create.3 — Lifecycle, config (event_queue_size, bot_token, gateway_url, api_base_url, intents, shards).
  - discord_next_event.3 — Event dequeuing, all 35 Discord event types.
  - discord_create_message.3 — REST API helpers (create/edit/delete message, get channel, get guild, reactions, interactions, DMs).
  - discord_gateway_connect.3 — Gateway WebSocket connection via librest binary channel.
  - discord_gateway_send_identify.3 — Identify payload with token, intents, sharding.
  - discord_gateway_send_heartbeat.3 — Heartbeat with sequence number and timing.
  - discord_gateway_send_resume.3 — Session resume after disconnect.
  - discord_gateway_process_heartbeat.3 — Heartbeat timing check helper.
  - discord_gateway_state.3 — Gateway state accessor.
  - discord_types.3 — Permission flags, intent bitfields, channel types, message types.
- Headers in `include/`: discord.h, discord_types.h
- All public functions follow strict C conventions: check returns for NULL/0/-1, events carry embedded arrays (copy-safe, no ownership transfer).

## Harness / Testing Infrastructure

- Smoke test (`tests/test_discord_smoke.c`): lifecycle, NULL handling, config validation, event queue drain, API helper NULL checks, gateway state accessor, reset, set_token, event struct copy safety.
- Dialectic test (`tests/test_discord_dialectic.c`): paired client↔server buffer exchange — REST round-trip, Gateway handshake, multiple requests, new REST helper frame production, embedded event field verification.
- Gateway lifecycle test (`tests/test_discord_gateway_lifecycle.c`): heartbeat timing logic, identify intents, gateway state transitions, event type completeness (35 types), interaction event fields.
- Fuzz harness (`tests/fuzz_discord.c`): 3 targets (API surface including new helpers, event translation, config fuzzing with intents/shards).
- CMake + ctest: builds static library (discord), runs all tests under strict -Wall -Werror.
- Valgrind integration: `cmake -DENABLE_VALGRIND=ON` runs all tests under Valgrind with `--leak-check=full --error-exitcode=1`.

All docs emphasize the plumbing model: library emits structured events and raw protocol data; caller owns decisions, I/O, and JSON parsing.