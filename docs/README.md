# libdiscord Documentation

This library provides a pure-C, plumbing-style Discord bot support library on top of librest and shaggy (libhttp2).

## Entry Points

- [AGENTS.md](../AGENTS.md) (root) — Start here: build, test, agent rules, definition of done.
- [ARCHITECTURE.md](../ARCHITECTURE.md) — Module boundaries, invariants, plumbing philosophy (ADR 006), event model, librest integration.
- [DOMAIN.md](DOMAIN.md) — Discord domain glossary: REST API, Gateway, events, intents, embeds, interactions.

## Architecture Decision Records

- [001-agent-ready-documentation.md](decisions/001-agent-ready-documentation.md) — Agent-ready documentation scaffold
- [002-event-loop-compatibility.md](decisions/002-event-loop-compatibility.md) — Event loop compatibility requirements
- [003-testing-fuzzing-valgrind.md](decisions/003-testing-fuzzing-valgrind.md) — Testing, fuzzing, and Valgrind policy
- [004-dialectic-client-server-testing.md](decisions/004-dialectic-client-server-testing.md) — Paired client/server buffer-exchange testing
- [006-core-library-as-plumbing-pdu-stack.md](decisions/006-core-library-as-plumbing-pdu-stack.md) — Core as thin PDU parser (ADR 006)
- [007-documentation-and-manpage-updates.md](decisions/007-documentation-and-manpage-updates.md) — Documentation and manpages updated with API changes
- [008-c-only-examples-and-codebase.md](decisions/008-c-only-examples-and-codebase.md) — Strict C language requirement (no C++)
- [009-consistent-protocol-interfaces.md](decisions/009-consistent-protocol-interfaces.md) — Unified `*_config_t` + `*_next_event` interface
- [010-c-interfaces-and-language-bindings.md](decisions/010-c-interfaces-and-language-bindings.md) — Hanson's *C Interfaces and Implementations* principles + FFI-friendly design

## API Documentation

- Manpages (section 3): See `man/man3/` (or installed under ${CMAKE_INSTALL_MANDIR}/man3).
  - discord_create.3 — Lifecycle, config (event_queue_size, bot_token, gateway_url, api_base_url).
  - discord_next_event.3 — Event dequeuing, all Discord event types (READY, MESSAGE_CREATE, API_RESPONSE, etc.).
  - discord_create_message.3 — REST API helpers (create/edit/delete message, get channel).
- Headers in `include/`: discord.h, discord_types.h
- All public functions follow strict C conventions: check returns for NULL/0/-1, events carry const data pointers (no ownership transfer unless documented).

## Harness / Testing Infrastructure

- Smoke test (`tests/test_discord_smoke.c`): lifecycle, NULL handling, config validation, event queue drain, API helper NULL checks.
- Dialectic test (`tests/test_discord_dialectic.c`): paired client↔server buffer exchange — REST round-trip, Gateway handshake, multiple requests.
- Fuzz harness (`tests/fuzz_discord.c`): 3 targets (API surface, event translation, config fuzzing).
- CMake + ctest: builds static library (discord), runs all tests under strict -Wall -Werror.

All docs emphasize the plumbing model: library emits structured events and raw protocol data; caller owns decisions, I/O, and JSON parsing.
