# ADR 0008: Web UI Architecture

## Status

Accepted

## Context

Users need a web-based interface for easier file sharing and device management, especially for non-CLI users.

## Decision

We implemented a React-based web UI with:
- Vite as build tool
- TypeScript for type safety
- REST API backend for device communication
- Responsive design for desktop and mobile browsers

## Consequences

- Accessible from any modern web browser
- No client installation required
- Can run on headless servers
- API server provides programmatic access

## Alternatives Considered

1. **Electron app** - Rejected due to resource overhead
2. **Native web components** - Rejected due to framework maturity
3. **Server-side rendering** - Rejected for simplicity
