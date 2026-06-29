# ADR 002: Event Loop Compatibility

**Date**: 2026-06-29
**Status**: Accepted
**Deciders**: Project maintainers

## Context
libdiscord is a Discord bot library that must integrate with various event loops (epoll, io_uring, libuv, libev, select, etc.) on Linux, macOS, and embedded platforms. The library must not assume any particular I/O model.

## Decision
- The library is pure plumbing: it consumes/produces byte buffers only.
- All networking I/O (TCP, TLS, DNS) lives exclusively in the calling application.
- The caller feeds raw bytes via `discord_feed_input()` and retrieves output via `discord_get_output()`.
- No event loop library is linked or required.
- No threads, no timers, no blocking calls inside the library.

## Consequences
- The library can be used with any event loop or even in bare-metal embedded contexts.
- The caller is responsible for managing connection lifecycle, TLS handshake, and reconnection logic.
- Heartbeat timing is caller-managed: `discord_gateway_process_heartbeat()` is a pure check, not a timer.

## Verification
- The library compiles and links without any system library dependencies beyond libc (stdlib, string, stdio).
- Dialectic tests demonstrate pure buffer exchange without any I/O.
