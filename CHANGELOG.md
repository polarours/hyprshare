# Changelog

All notable changes to hyprshare are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

- **Fixed front-end contract bugs**: Tools file search now passes `--json` and
  reads the `{results}` envelope; the Network panel reads real
  `is_up`/`netmask`/`gateway` fields; Settings backend badges derive from
  `ready`/`support_tier`. Clipboard Read fills the editor from the returned
  `text`, and Push to peer pushes content first — the CLI's `push_to_peer` now
  reads persistent history so the cross-process send works. Sessions serving
  binds `0.0.0.0` and shows the machine's LAN IP.
- **Fixed TUI input bugs**: `w` is now typeable inside clipboard write mode
  (Esc exits), Unicode history is truncated by chars (no byte-slice panic),
  and failed CLI loads surface an error in the status bar instead of silently
  showing zeros.
- **Fixed backend modules that faked results**: `batch` runs real commands;
  `audit` performs real permission/network/service checks; `backup` reads real
  manifest metadata; `sync` auto-syncs changed files; `cloud` reports transfer
  status; `netdiag` reports real interface addresses; `mobile`/`remote` guard
  shared client lists and invoke the frame callback; `perf`/`profiling` fixed
  a rate-limiter infinite loop and live throughput.

## [0.1.0] - 2026-11-21

Initial release. hyprshare is a Linux-native sharing platform for Hyprland and
Wayland, with a CLI-first core and four front-ends over the same commands: the
`hyprshare` CLI, the `hyprshare-tui` terminal UI, the `hyprshare-desktop` GUI,
and the `hyprshare web serve` web interface. One install provides all four.

### Core sharing

- **File inbox** with an HTTP PUT receiver: tokens, size limits, announced
  discovery, checksum verification, archive extraction, and a persistent
  transfer history (`hyprshare inbox serve|receive|put|history|extract`).
- **Share sessions**: stage a file, folder, or clipboard entry, serve it over
  the network with a generated token and a lifetime, and let a peer fetch it
  with payload verification (`hyprshare share file|folder|clipboard`,
  `session serve|fetch|show|verify|probe`).
- **Peer identity and trust store**: per-device identity, trusted peers,
  offline invitations, pairing, export and import (`hyprshare peer …`).
- **Discovery** of peers on the local network through mDNS/Avahi, both
  announcing and browsing (`hyprshare discovery announce|browse`).
- **TLS/mTLS transport** built on OpenSSL with self-signed X.509 v3
  certificates; used by the inbox, the sessions, and the HTTP client
  (`hyprshare tls generate|verify|info`).
- **Clipboard**: read and write through wl-clipboard/xclip/xsel, plus
  cross-device exchange with persistent history
  (`hyprshare clipboard read|write`, `clipboard-sync send|receive|history`).
- **File synchronization** rules that push/pull files to a peer over the inbox
  protocol (`hyprshare sync …`).
- **Screen capture** through xdg-desktop-portal and PipeWire, with an OBS
  virtual-camera fallback, and **screen recording** to MP4/WebM/MKV via ffmpeg
  (`hyprshare record …`).

### Desktop, web, and terminal

- **Tauri desktop application** (`hyprshare-desktop`): a macOS-style GUI with
  light/dark/system themes; pages for Dashboard, File Transfer, Clipboard,
  Devices, Sessions, Tools and Settings, plus device detail. Native file
  pickers, session serving with a generated token, and payload verification.
- **Web interface**: REST API served by `hyprshare web serve` (status, health,
  config, backends, peers, inbox, upload) with a React frontend; the same
  frontend is embedded in the desktop app.
- **Terminal UI** (`hyprshare-tui`): a ratatui dashboard for device, inbox and
  clipboard, with keys to start a receiver and clear transfer history.

Every frontend is a subprocess caller of the CLI, so actions behave identically.

### Services and operations

- **Daemon** (`hyprshared`): a Unix-socket daemon hosting inbox listeners,
  reachable from the CLI (`hyprshare daemon …`), with systemd user units.
- **Cloud sync** with a `local` directory provider and an `http` object store.
- **Self-update** from a release manifest on disk or over HTTP.
- **Backup and restore** of hyprshare state.
- **Configuration profiles** and full config read/write.
- **File search** with glob patterns, size filters, and sorting.
- **System monitor** reporting CPU, memory, disk, network and connection
  counters read from the kernel.
- **Diagnostics**: doctor health checks, a security auditor, network
  diagnostics, and support bundles.
- **Plugin system**: shared objects contribute CLI commands at runtime via
  `dlopen()`, with a documented ABI and an example plugin.
- **Mobile integration**: transfer and remote-control servers with Android
  (Kotlin) and iOS (Swift) client sources.
- **Interactive shell**, batch runner, and span-based profiling.

### Packaging and tooling

- **One AUR package** (`hyprshare`) provides the CLI, daemon, TUI, desktop GUI
  and web UI; CPack produces DEB packages, with Fedora and Nix scaffolding.
- **CMake install rules** for binaries, bash/zsh/fish completions, systemd user
  units, and documentation.
- **13 CTest suites** covering core, TLS, HTTP, cloud, sync, recording,
  monitor, update, web, daemon, service, CLI dispatch, and plugins.
- **CI** builds, tests and packages a DEB; **CPack** is configured so the
  package job emits a real artifact.
- **Documentation**: architecture overview with an implementation status
  table, ADRs, threat model, compatibility matrix, mobile protocol
  specification, API reference, user guide, and a community-showcase README
  with a real screenshot wall.

### Notable implementation details

- Clipboard reads on Wayland keep a valid stdin fd — the previous closed-fd
  setup made wl-clipboard abort with "closed standard file descriptor".
- `share file|folder|clipboard` stage a session by default; `--dry-run` inspects
  and `--stage` is a compatibility no-op.
- `ProcessRunner::run_shell()` uses a non-login shell (`-c`), avoiding the
  heavy profile initialization that made every CLI status call spawn dozens of
  processes.
