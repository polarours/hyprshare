# ADR 0007: TLS/mTLS Transport Layer

## Status

Accepted

## Context

The existing file-share transport used plaintext TCP, which is insecure for network transfers. We needed to add authentication and encryption without breaking the existing protocol.

## Decision

We added a TLS transport layer using OpenSSL with:
- Self-signed device certificates generated on first run
- mTLS (mutual TLS) for both client and server authentication
- Certificate fingerprint verification against the trust store
- `--insecure` flag for local/testing use

## Consequences

- All TCP connections can now be encrypted
- Device identity is cryptographically verifiable
- Existing `HYPRSHARE_OFFER/1` protocol runs inside TLS tunnel
- Backward compatible via `--insecure` flag

## Alternatives Considered

1. **Noise protocol framework** - Rejected due to complexity
2. **SSH tunneling** - Rejected due to external dependency
3. **Application-layer encryption** - Rejected in favor of standard TLS
