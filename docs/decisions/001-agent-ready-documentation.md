# ADR 001: Adopt Agent-Ready Documentation Scaffold

**Date**: 2026-06-29
**Status**: Accepted
**Deciders**: Initial project bootstrap (Hermes agent under human direction)

## Context
libdiscord is intended to be developed and maintained primarily by coding agents (Hermes + future agents). Without explicit repo context, agents repeatedly miss local conventions, architecture boundaries, and domain language. Damian Galarza's agent-ready principles provide a proven progressive-disclosure structure that gives agents the same onboarding a new human engineer would receive.

## Decision
We adopt the full agent-ready scaffold:
- AGENTS.md as single source of truth and agent entry point
- ARCHITECTURE.md codemap
- docs/ hierarchy (README, DOMAIN, decisions/)
- Starter ADR recording this choice

## Consequences
- Every future change must keep these documents accurate.
- Human review of DOMAIN.md is required before large feature work.
- Agents are instructed (in AGENTS.md) to consult the map before coding.
- This adds a small documentation maintenance burden but dramatically improves agent output quality and reduces context thrashing.

## Alternatives Considered
- No documentation layer → agents would require constant human steering.
- Heavy Doxygen + wiki → too much overhead for a small plumbing library; progressive disclosure is lighter and more agent-effective.

This decision is recorded so future agents understand why the repo looks the way it does.
