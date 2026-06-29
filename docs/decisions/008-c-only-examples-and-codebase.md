# ADR 008: C-Only Examples and Codebase

**Date**: 2026-06-29
**Status**: Accepted
**Deciders**: Project maintainers

## Context
libdiscord is a pure C library. Examples, tests, and all code must be valid C11 — no C++ features, no C++ headers, no C++ reserved words in public APIs.

## Decision
- All source files (`.c`, `.h`) must compile as C11 with `-Wall -Wextra -Wpedantic -Werror`.
- Examples demonstrate the library in pure C — no C++ wrappers.
- Public headers use `extern "C"` guards for C++ consumers, but the implementation is pure C.
- No `bool`, `nullptr`, `auto`, `class`, `template`, or other C++ reserved words in public API names.
- Use `int` for boolean returns (0 success, -1 error) and event availability (1 available, 0 not).

## Consequences
- Maximum portability: compiles on any C11 compiler.
- FFI-friendly: no C++ name mangling issues.
- Examples serve as documentation for C developers.

## Verification
- `cmake -B build -S . && cmake --build build` succeeds with strict warnings.
- No C++ compiler required.
