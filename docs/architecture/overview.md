# Architecture Overview

## Core Shape

The repository is a Linux-native sharing platform built around a CLI-first
core. The libraries are split by domain:

- `hyprshare_model`: domain enums and DTOs.
- `hyprshare_platform_linux`: paths, process execution, and network helpers.
- `hyprshare_core`: config loading, backend registry, adapter registry, discovery, inbox, file sharing, clipboard, trust store, planner, and launcher.
- `hyprshare_diagnostics`: CheckRegistry, environment checks, network diagnostics, and support bundle generation.
- `hyprshare_tls`: TLS/mTLS transport layer with self-signed certificates.
- `hyprshare_daemon`: Unix socket IPC daemon with systemd integration.
- `hyprshare_portal`: xdg-desktop-portal and PipeWire integration.
- `hyprshare_mobile`: Mobile transfer and remote control servers.
- `hyprshare_sync`: File synchronization and clipboard synchronization services.
- `hyprshare_recording`: Screen recording (see the status table below).
- `hyprshare_web`: REST API server for the web interface.
- `hyprshare_shell`: Interactive shell.
- `hyprshare_batch`: Batch command execution.
- `hyprshare_plugin`: Plugin loading with a persistent registry.
- `hyprshare_perf`: Buffer pool, progress tracking, and rate limiting.
- `hyprshare_monitor`: System metrics collection.
- `hyprshare_security`: Security auditor.
- `hyprshare_backup`: Backup and restore.
- `hyprshare_log`: Leveled logging.
- `hyprshare_config_validator`: Configuration validation rules.
- `hyprshare_error`: Retry manager with exponential backoff.
- `hyprshare_update`: Release version checking.
- `hyprshare_http`: HTTP/1.1 client used by the cloud and the update checker.
- `hyprshare_cloud`: Cloud upload, download, and synchronization.
- `hyprshare_profile`: Multi-profile configuration management.
- `hyprshare_search`: File search with filtering and sorting.
- `hyprshare_profiling`: Span based performance profiling.
- `hyprshare` CLI: the public command surface, 32 top-level commands.
- `hyprshared`: daemon binary for background services.

## Implementation Status

Not every capability is finished. The table states what each one actually does
today, so that nothing here has to be taken on trust.

| Capability | Status | Notes |
|------------|--------|-------|
| CLI command surface | Implemented | 32 top-level commands, each with generated usage text |
| Configuration | Implemented | `config show`, `config set`, `config get` |
| File sharing and inbox | Implemented | HTTP PUT inbox, sessions, export/import, verification |
| Peer identity and trust store | Implemented | Device identity, trust store, offline invitations |
| Discovery | Implemented | mDNS/Avahi based |
| TLS/mTLS transport | Implemented | OpenSSL with self-signed certificates, used by the inbox and the HTTP client |
| Screen capture | Implemented | xdg-desktop-portal plus PipeWire, with an OBS fallback |
| Clipboard read/write | Implemented | Through wl-clipboard, xclip, or xsel |

| Plugin system | Implemented | Shared objects exporting `hyprshare_plugin_create`, registered persistently |
| Diagnostics and audit | Implemented | Check registry, network diagnostics, security auditor |
| Daemon | Implemented | A Unix socket daemon that hosts inbox listeners, reachable with `hyprshare daemon` |
| Backup and restore | Implemented | Create, restore, delete, list, inspect |
| Web interface | Implemented | A REST API served by `hyprshare web serve` and a React frontend in `web/` that talks to it |
| Desktop application | Implemented | Tauri application in `desktop/` |
| Mobile clients | Implemented | Android (Kotlin) and iOS (Swift) clients in `mobile/` |
| File synchronization | Implemented | Files are transferred to a peer over the inbox protocol, in both directions |
| Clipboard synchronization | Implemented | The service polls the system clipboard, persists a history, and exchanges the content with a peer over the inbox |
| Cloud sync | Implemented | A provider is either `local`, which keeps objects in a directory, or `http`, which stores them on an HTTP server |
| Update checking | Implemented | Reads a release manifest from disk or over HTTP, and downloads the release artifact |
| Screen recording | Implemented | The PipeWire stream negotiates a video format and the frames are encoded into MP4, WebM, or Matroska with ffmpeg |

## Current Runtime Model

The CLI owns the process and provides:

1. **System Status**: `hyprshare status` shows system configuration.
2. **Diagnostics**: `hyprshare doctor` checks 20+ system health indicators.
3. **Configuration**: `hyprshare config show/set/get` manages settings.
4. **Discovery**: mDNS/Avahi-based device discovery with `_hyprshare._tcp`.
5. **Inbox**: HTTP PUT file sharing with optional idle timeout and token auth.
6. **Session Management**: File share sessions with export/import/verify.
7. **Peer Management**: Device identity, trust store, and offline invites.
8. **Screen Sharing**: Portal-based screen capture with OBS fallback.
9. **Clipboard**: Text clipboard read/write through wl-clipboard/xclip/xsel.
10. **TLS Transport**: mTLS with self-signed certificates.
11. **Daemon**: Background service with Unix socket IPC.
12. **Mobile Integration**: File transfer and remote control for Android/iOS.
13. **Plugins**: Loadable shared objects that contribute CLI commands.
14. **Web Interface**: React-based UI with REST API backend.
15. **Desktop Application**: Tauri-based native desktop app.

## Library Architecture

```
hyprshare
├── hyprshare_model          # Domain types
├── hyprshare_platform_linux # Platform helpers
├── hyprshare_core           # Core functionality
├── hyprshare_diagnostics    # Health checks
├── hyprshare_tls            # TLS transport
├── hyprshare_daemon         # Background service
├── hyprshare_portal         # Portal/PipeWire
├── hyprshare_mobile         # Mobile integration
├── hyprshare_sync           # Sync services
├── hyprshare_recording      # Screen recording
├── hyprshare_web            # REST API
├── hyprshare_shell          # Interactive shell
├── hyprshare_batch          # Batch execution
├── hyprshare_plugin         # Plugin loading
├── hyprshare_perf           # Performance primitives
├── hyprshare_monitor        # System metrics
├── hyprshare_security       # Security audit
├── hyprshare_backup         # Backup and restore
├── hyprshare_log            # Logging
├── hyprshare_config_validator # Config validation
├── hyprshare_error          # Retry handling
├── hyprshare_update         # Release checking
├── hyprshare_http           # HTTP client
├── hyprshare_cloud          # Cloud sync
├── hyprshare_profile        # Configuration profiles
├── hyprshare_search         # File search
├── hyprshare_profiling      # Performance profiling
└── hyprshare                # CLI binary
```

## Data-Driven Control Plane

- `data/backends/*.yaml`: backend capability metadata and prerequisites.
- `data/compat/apps/*.yaml`: app matching, launch commands, and fallback chains.

## State on Disk

- `~/.config/hyprshare/config.toml`: main configuration.
- `~/.config/hyprshare/plugins.list`: registered plugins and their enabled state.
- `~/.local/state/hyprshare/`: inbox history, clipboard history, backups, cloud objects, and downloads.
- `~/.config/hyprshare/plugins.list`: the registered plugins and their state.
- `http://127.0.0.1:8080/api`: the REST API of the web interface.
- `~/.local/state/hyprshare/cloud/<provider>/`: the local directory that backs a cloud provider.

## Mobile Protocol

- Newline-delimited JSON over TLS
- Message types: discover, auth, file transfer, screen sharing, remote control, clipboard sync
- Supports Android (Kotlin) and iOS (Swift) clients

## Near-Term Expansion

- Applying a release automatically, which still needs an installer
- Performance optimization for file transfers
- Enhanced security audit
- User feedback system
- Additional platform support
