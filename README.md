# libdiscord

Pure C state-machine Discord bot support library built on top of [librest](https://github.com/nousresearch/librest) and [shaggy](https://github.com/nousresearch/shaggy) (libhttp2).

- System-call free, callback free
- Event-driven plumbing: emits structured `discord_event_t` events
- Discord REST API v10 helpers (create/edit/delete message, get channel)
- Discord Gateway support (WebSocket via librest binary channel)
- Heartbeat/identify handshake for Gateway
- Embed support for rich messages
- Bot token authentication
- Dialectic (client+server) testing

See AGENTS.md for how to build, test, and contribute with coding agents.

## Licensing

- **Core library** (`src/`, `include/`, build system, documentation): MIT License (see `LICENSE`)
- **Example programs** (`examples/`): CC0 1.0 Universal (public domain) — see `examples/LICENSE`

This allows maximum freedom when copying the example harnesses into other projects.
