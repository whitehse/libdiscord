# ADR 007: Documentation and Manpage Updates

**Date**: 2026-06-29
**Status**: Accepted
**Deciders**: Project maintainers

## Context
As the libdiscord API evolves, documentation must stay synchronized with code changes. Stale docs are worse than no docs — they actively mislead agents and humans.

## Decision
- Every API change must update the corresponding manpage in `man/man3/`.
- AGENTS.md, ARCHITECTURE.md, and docs/ must be reviewed and updated when architecture or domain assumptions change.
- New public functions get a manpage entry.
- Manpages follow the standard `*.3` format: NAME, SYNOPSIS, DESCRIPTION, RETURN VALUES, EXAMPLES, SEE ALSO, NOTES.
- The docs/README.md index must link to all manpages and ADRs.

## Consequences
- Small documentation burden on every change, but prevents drift.
- Agents can discover API details from manpages without reading source code.
- Human developers get standard Unix-style API documentation.

## Verification
- Manpages exist for all public API functions.
- docs/README.md links are valid.
