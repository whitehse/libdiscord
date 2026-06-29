# ADR 010: C Interfaces and Implementations + Language Binding Friendly Design

**Date**: 2026-06-29
**Status**: Accepted
**Deciders**: Project maintainers

## Context
libdiscord is a pure C library. As the project matures, it is important to establish long-term design principles for the public API that make it reusable and easy to consume from other languages via FFI.

Two influences are particularly relevant:

1. **C Interfaces and Implementations** (David R. Hanson, 1996) — A foundational text on writing reusable, well-engineered C libraries. It emphasizes opaque types, consistent naming, minimal interfaces, clear ownership, and separation of interface from implementation.

2. **Language Binding Friendliness** — Modern libraries are frequently consumed via FFI from Rust, Go, Python, Zig, and other languages. Poor C API design makes binding generation difficult or unsafe.

## Decision
The library shall adopt the following principles:

### From *C Interfaces and Implementations*
- **Opaque types**: `discord_ctx_t` remains completely opaque. Clients never access struct members directly.
- **Consistent naming**: All public symbols use the `discord_` prefix.
- **Minimal interfaces**: Each module exposes only what is necessary. Implementation details stay in `*.c` files.
- **Clear ownership**: Functions that return newly allocated objects document who is responsible for calling the corresponding `*_destroy` function.
- **Error handling**: Prefer explicit return values over hidden global state.

### Language Binding Support
- Public headers avoid:
  - Complex macros that expand to code
  - Bitfields in public structures
  - Inline functions that expose implementation details
  - C++ reserved words or GNU extensions in public APIs
- All handles are passed as pointers (`T *`).
- Function signatures should be straightforward for tools like `bindgen`, `cffi`, and `ctypes`.
- Event structures use simple unions and fixed-size types.

## Consequences
- Future changes to `include/discord.h` must be reviewed against these constraints.
- The `discord_event_t` union and config structs are intentionally simple.
- Documentation must explicitly state ownership and error semantics.
- When adding new features, preference will be given to designs that remain easy to bind.

## Verification
- All public headers use opaque types and consistent prefixes.
- New public functions follow the established naming and ownership patterns.
