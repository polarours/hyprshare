# ADR 0009: File Synchronization

## Status

Accepted. Partially implemented: sync rules and a watch loop exist, but the
transport between devices is not wired up.

## Context

Users need automatic folder synchronization between devices for seamless file sharing workflows.

## Decision

We implemented a file sync service with:
- Directory watching with configurable intervals
- Bidirectional sync rules
- Local-to-remote and remote-to-local sync directions
- Event-based notifications

## Consequences

- Automatic file synchronization between devices
- Configurable sync rules per directory
- Low overhead with polling-based watching

## Alternatives Considered

1. **Real-time file system events** - Rejected due to platform complexity
2. **Periodic manual sync** - Rejected for user convenience
3. **Cloud-based sync** - Rejected for privacy
