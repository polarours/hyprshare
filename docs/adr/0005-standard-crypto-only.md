# ADR 0005: Standard Crypto Only

## Status

Accepted

## Decision

`hyprshare` will only use established cryptographic building blocks and transport protocols. It will not invent custom encryption or signature schemes.

## Rationale

- The project's value is orchestration, trust flow, policy, and supportability.
- Novel cryptography would add risk without addressing the real product problem.
- Standard TLS and pinned device identity are sufficient for the planned peer-sharing model.

