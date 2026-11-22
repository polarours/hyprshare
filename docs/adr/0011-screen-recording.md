# ADR 0011: Screen Recording

## Status

Accepted. Implemented: screen capture uses xdg-desktop-portal + PipeWire with
an OBS virtual-camera fallback, and frames are encoded to MP4/WebM/MKV with
ffmpeg. On hosts without a Wayland portal or PipeWire, `record start` reports
the missing capability instead of claiming a recording is running.

## Context

Users need screen recording functionality for capturing and sharing screen content.

## Decision

We implemented a screen recorder with:
- Multiple format support (MP4, WebM, MKV)
- Configurable resolution and frame rate
- Pause/resume functionality
- Event-based status notifications

## Consequences

- Native screen recording capability
- Flexible format and quality options
- Integration with file sharing workflow

## Alternatives Considered

1. **External tool integration** - Rejected for consistency
2. **Browser-based recording** - Rejected for quality
3. **Third-party library** - Rejected for dependencies
