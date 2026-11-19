# ADR 0002: Hyprland First and Linux Native

## Status

Accepted

## Decision

The project will optimize first for Hyprland and Wayland on Linux, using Linux-native APIs and services directly.

## Rationale

- Hyprland is the initial operational pain point.
- Linux-native specialization keeps the control plane honest and useful.
- Premature cross-platform abstraction would weaken the architecture before the target environment works well.

