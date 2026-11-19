# ADR 0001: Platform, Not Script Collection

## Status

Accepted

## Decision

`hyprshare` will be built as a modular platform with explicit domain, compatibility, diagnostics, and runtime layers rather than as a loose set of shell scripts.

## Rationale

- The problem is recurring orchestration and compatibility drift, not a one-time setup sequence.
- The project needs stable command semantics, curated data, and maintainable subsystem boundaries.
- Diagnostics, repair, policy, and future trust/session work all depend on shared domain concepts.

