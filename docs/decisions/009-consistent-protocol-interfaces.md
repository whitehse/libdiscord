# ADR 009: Consistent Protocol Interfaces

**Date**: 2026-06-29
**Status**: Accepted
**Deciders**: Project maintainers

## Context
librest and shaggy expose a consistent interface shape across all protocol modules: `*_config_t`, `*_create(role)`, `*_create_with_config(role, config)`, `*_feed_input(ctx, data, len)`, `*_next_event(ctx, &event)`, `*_get_output(ctx, buf, max)`. libdiscord must follow the same pattern for consistency.

## Decision
libdiscord exposes the same interface shape:
- `discord_config_t` with `event_queue_size`, `bot_token`, `gateway_url`, `api_base_url`
- `discord_create(role)` and `discord_create_with_config(role, config)`
- `discord_feed_input(ctx, data, len)`
- `discord_next_event(ctx, &event)` returning 1 if event available, 0 otherwise
- `discord_get_output(ctx, buf, max)`
- Discord-specific helpers layered on top (REST API, Gateway)

This makes libdiscord feel like a natural extension of the librest/shaggy stack.

## Consequences
- Developers familiar with librest can immediately use libdiscord.
- The consistent shape enables composability: libdiscord wraps librest, which wraps shaggy.
- All layers use the same buffer-in/buffer-out model.

## Verification
- Public API in `discord.h` follows the consistent shape.
- AGENTS.md documents the interface direction.
