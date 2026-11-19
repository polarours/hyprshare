# ADR 0012: GUI Desktop Application

## Status

Accepted

## Context

Users need a native desktop application for easier file sharing and device management.

## Decision

We implemented a Tauri-based desktop application with:
- Rust backend for system integration
- Web frontend for UI
- Native window management
- System tray integration

## Consequences

- Native desktop experience
- Small binary size
- Cross-platform support
- Easy updates via package managers

## Alternatives Considered

1. **Electron app** - Rejected due to resource overhead
2. **GTK/Qt app** - Rejected for complexity
3. **Web app only** - Rejected for native features
