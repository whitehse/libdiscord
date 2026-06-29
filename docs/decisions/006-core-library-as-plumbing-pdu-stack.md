# ADR 006: Core Library as Plumbing — Pure Discord Protocol Support

**Date**: 2026-06-29
**Status**: Accepted
**Deciders**: Project maintainers

## Context
libdiscord provides Discord bot support on top of librest and shaggy. The temptation is to add high-level features like JSON parsing, event caching, rate limiting, and automatic reconnection. However, these would violate the pure plumbing model established by librest and shaggy.

## Decision
libdiscord shall act strictly as **plumbing** for Discord's protocol.

### Key Principles
1. **Minimal Active Code** — The library encodes/decodes Discord protocol messages. It does not decide what to do with the data.
2. **Raw JSON Emission** — Gateway events and API responses are emitted as raw JSON strings. The caller owns parsing.
3. **Event-Driven Return** — Instead of performing actions, the library emits structured events. The caller is responsible for acting on them.
4. **Caller Owns All Policy** — Rate limiting, reconnection, caching, sharding, and all other bot logic live in the application.
5. **No JSON Parser** — The library includes only minimal string-search extraction for essential fields (op, s, t in Gateway payloads). Full JSON parsing is the caller's responsibility.

## Rationale
- Maximizes reusability: the same library can power bots, webhooks, monitoring tools, and testing harnesses.
- Prevents the library from accumulating domain-specific behavior.
- Aligns with librest and shaggy's plumbing philosophy.
- Keeps the library small, testable, and fuzzable.

## Consequences
- No `cJSON`, `jansson`, or other JSON library dependency.
- No in-memory guild/channel/member cache.
- No automatic rate limit handling.
- No automatic reconnection or resume logic (caller decides).
- Minimal field extraction (op, s, t, session_id, heartbeat_interval) is the only "smart" behavior.
