# ADR 0003: CLI First With Daemon-Shaped Core

## Status

Accepted

## Decision

The first implementation remains CLI-first, but its internal services, state ownership, and planning model are shaped so they can migrate into a user-level daemon cleanly.

## Rationale

- A daemon is valuable long term for discovery, trust, and inbound sessions.
- A CLI-first start reduces bootstrap cost and keeps early debugging transparent.
- The domain model should not need rewriting when the daemon lands.

