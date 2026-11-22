# ADR 0010: Clipboard Synchronization

## Status

Accepted. The service polls the system clipboard, persists a history, and
exchanges content with a remote device over the HTTP inbox (`clipboard-sync
send|receive` and `clipboard write|read`). Cross-device sync is implemented.

## Context

Users need cross-device clipboard synchronization for seamless text sharing between devices.

## Decision

We implemented a clipboard sync service with:
- Bidirectional clipboard content synchronization
- History tracking for clipboard entries
- MIME type support for different content types
- Real-time sync via polling

## Consequences

- Seamless clipboard sharing between devices
- History for clipboard content recovery
- Support for text and basic content types

## Alternatives Considered

1. **System clipboard hooks** - Rejected due to platform complexity
2. **Manual clipboard copy** - Rejected for user convenience
3. **Cloud-based clipboard** - Rejected for privacy
