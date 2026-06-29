# ADR 003: Testing, Fuzzing, and Valgrind Policy

**Date**: 2026-06-29
**Status**: Accepted
**Deciders**: Project maintainers

## Context
libdiscord deals with external input (Discord API responses, Gateway WebSocket frames). Robust testing is essential to prevent crashes, memory leaks, and undefined behavior.

## Decision
Every change to core library files (`src/discord.c`, `include/discord.h`) must:
1. Add or update tests in `tests/`.
2. Pass `ctest` before the change is considered complete.
3. Pass under Valgrind with zero leaks and zero memory errors.

Testing layers:
- **Smoke tests** (`test_discord_smoke.c`): individual API surface tests, NULL handling, config validation, event queue drain.
- **Dialectic tests** (`test_discord_dialectic.c`): paired client↔server buffer exchange — REST round-trip, Gateway handshake, multiple requests.
- **Fuzz harness** (`fuzz_discord.c`): 3 targets (API surface, event translation, config fuzzing). Built with `-fsanitize=fuzzer,address` via `cmake -DENABLE_FUZZ=ON`.

## Consequences
- No change is complete without test verification.
- Fuzz harness must be run after significant protocol changes.
- Valgrind is the baseline memory safety check for every release.

## Verification
- CI runs `ctest` on every commit.
- Periodic fuzz runs target millions of iterations with no crashes.
