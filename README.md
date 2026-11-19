# hyprshare

`hyprshare` is a serious, Linux-native sharing platform for Hyprland and Wayland environments. It aims to unify screen sharing, capability sharing, diagnostics, compatibility knowledge, and long-term peer-sharing workflows behind a coherent CLI-first architecture instead of a pile of scripts.

## Features

- **CLI-first architecture** with 32 top-level commands and a command registry
- **Extensible diagnostics** with CheckRegistry and `--fail-on` flag
- **Daemon support** via `hyprshared` with Unix socket IPC
- **TLS/mTLS transport** for authenticated peer connections
- **Portal integration** with xdg-desktop-portal and PipeWire
- **Data-driven compatibility** with backend and app profiles
- **Plugin system** where shared objects contribute their own CLI commands

Not every capability is finished yet. [docs/architecture/overview.md](docs/architecture/overview.md)
carries an implementation status table that states, for each capability,
whether it is implemented, partial, or still a placeholder.

## Quick Start

```bash
# Build
cmake -S . -B build -G Ninja
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure

# Check system health
./build/hyprshare doctor

# Share a screen
./build/hyprshare share screen --dry-run
```

## CLI Commands

```bash
# Diagnostics
hyprshare status
hyprshare doctor --app wemeet
hyprshare doctor --fail-on warning
hyprshare check

# File sharing
hyprshare share file README.md
hyprshare share folder docs/
hyprshare share clipboard --stage

# Screen sharing
hyprshare share screen --app wemeet --dry-run

# Inbox
hyprshare inbox receive --port 28556
hyprshare inbox put <host:port> ./photo.jpg

# Peer management
hyprshare peer self
hyprshare peer list
hyprshare peer add debian-laptop-01 --label "Debian Laptop"

# Session management
hyprshare session list
hyprshare session serve <id> --generate-token

# Daemon
hyprshared  # Start daemon
```

## Architecture

- `src/lib/core/` - Core libraries (inbox, file_share, clipboard, discovery, trust_store)
- `src/lib/diagnostics/` - CheckRegistry and extensible checks
- `src/lib/daemon/` - Daemon with Unix socket IPC
- `src/lib/tls/` - TLS/mTLS transport layer
- `src/lib/portal/` - Portal and PipeWire integration
- `src/bin/hyprshare/` - CLI with command registry
- `src/bin/hyprshared/` - Daemon binary

## Packaging

Packaging files are provided for:
- Arch Linux (`packaging/arch/PKGBUILD`)
- Debian (`packaging/debian/`)
- Fedora (`packaging/fedora/hyprshare.spec`)
- Nix (`packaging/nix/default.nix`)

## Documentation

- [Architecture Overview](docs/architecture/overview.md)
- [Threat Model](docs/threat-model/threat-model.md)
- [Compatibility Matrix](docs/compatibility/matrix.md)
- [ADR Index](docs/adr/README.md)
- [Changelog](CHANGELOG.md)
