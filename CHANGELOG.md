# Changelog

All notable changes to hyprshare will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

- **Redesigned the README for discoverability**: centered hero with logo,
  badges, dot-navigation, an authentic GUI screenshot, a "Why hyprshare"
  section, feature/frontend tables, a Mermaid architecture diagram, and a
  30-second quickstart.
- **Share the clipboard from the GUI**: the File Transfer page gained a
  Clipboard button alongside File/Folder, staging the current clipboard text
  as a session. Also corrected stale "Devices page" wording to "Sessions".
- **New dedicated Sessions page in the GUI**: staged shares now have their
  own sidebar page (Serve / Details / Delete, payload verification via
  `session verify`, and Clear all). This was previously buried in the Devices
  page.
- **Fixed clipboard reads on Wayland**: `ProcessRunner::run_shell` was closing
  stdin in the spawned shell, which made wl-clipboard abort with "closed
  standard file descriptor" — `clipboard read` and `share clipboard` always
  failed. Stdin now points at `/dev/null` (valid fd, never blocks).
- **`share clipboard` now stages a session by default**, matching
  `share file|folder`; `--dry-run` inspects the raw text, `--stage` is a no-op.
  The `share --help` lines were corrected to show `--dry-run` for all three.
- **TUI can clear transfer history**: press `d` on the Inbox page, then `y`
  to confirm (or `n` / Esc to cancel). The list reloads automatically after
  clearing.
- **TUI can now start an inbox receiver**: press `s` on the Inbox page to
  spawn a background `inbox serve` with a fresh token. The status bar shows
  the port and token for the sender. The receive dir is created under
  `~/Downloads/hyprshare-tui` to avoid the CLI default's missing-directory
  failure.
- **Serve a share session straight from the GUI**: the Devices page now has a
  Serve button on every session. It generates a fresh token, starts
  `hyprshare session serve` as a detached background process, and shows the
  exact `session fetch` command for the receiver — closing the loop from
  staging to transfer without leaving the app.
- **Native file and folder pickers** replace hand-typed paths for sharing,
  invitation pairing, and the search directory.

- **`share file` and `share folder` now stage a session by default**, so the
  transfer chain is one command away; the staged output prints the exact
  `session serve` follow-up. The manifest-only inspection moved to `--dry-run`
  (consistent with `share screen --dry-run`), and `--stage` is still accepted.
  The misleading "Transport will be added later" notes are gone — the transport
  has existed since the session serving chain landed.

## [0.6.1] - 2026-11-19

- **One install, four front-ends**: the Arch package now builds and installs
  the CLI/daemon, a new `hyprshare-tui` terminal UI (ratatui dashboard for
  device, inbox and clipboard), the Tauri desktop GUI, and the React web UI.
  The web UI is now actually served by `hyprshare web serve`, which returns
  the frontend for `/` and SPA routes alongside the REST API, with path
  traversal protection.
- **AUR packaging consolidated** into a single `hyprshare` package.

## [0.6.0] - 2026-11-19

Initial release.

### Core sharing

- **File inbox** with an HTTP PUT receiver: tokens, size limits, announced
  discovery, checksum verification, archive extraction, and a persistent
  transfer history (`hyprshare inbox serve|receive|put|history|extract`).
- **Share sessions**: stage a file or folder, serve it over the network with a
  generated token and a lifetime, and let a peer fetch it
  (`hyprshare share file|folder --stage`, `session serve|fetch|inspect|probe`).
- **Peer identity and trust store**: per-device identity, trusted peers,
  offline invitations, pairing, export and import (`hyprshare peer …`).
- **Discovery** of peers on the local network through mDNS/Avahi, both
  announcing and browsing (`hyprshare discovery announce|browse`).
- **TLS/mTLS transport** built on OpenSSL with self-signed X.509 v3
  certificates carrying the extensions peer verification needs; used by the
  inbox, the sessions, and the HTTP client (`hyprshare tls generate|verify|info`).
- **Clipboard**: read and write through wl-clipboard, xclip, or xsel, plus
  cross-device exchange with a persistent history
  (`hyprshare clipboard read|write`, `clipboard-sync send|receive|history`).
- **File synchronization**: rules that push and pull files to a peer over the
  inbox protocol (`hyprshare sync …`).
- **Screen capture** through xdg-desktop-portal and PipeWire, with an OBS
  virtual-camera fallback for apps with broken direct capture.
- **Screen recording** that encodes captured frames into MP4, WebM, or
  Matroska with ffmpeg (`hyprshare record …`).

### Desktop and web

- **Tauri desktop application** (`hyprshare-desktop`): a macOS-style GUI with
  light, dark, and system-following appearance, six functional pages
  (Dashboard, File Transfer, Clipboard, Devices, Tools, Settings) and a device
  detail drill-down. Every GUI action is a pass-through to the CLI, so the
  interface and the command line never disagree.
- **Web interface**: a REST API served by `hyprshare web serve` (status,
  health, config, backends, peers, inbox, upload) with a React frontend in
  `web/`.

### Services and operations

- **Daemon** (`hyprshared`): a Unix-socket daemon that hosts inbox listeners
  and is reachable from the command line (`hyprshare daemon …`), with systemd
  user units included.
- **Cloud sync** with a `local` directory provider and an `http` object store
  (`hyprshare cloud configure|upload|download|sync|list|status`).
- **Self-update** from a release manifest on disk or over HTTP, including
  download and apply (`hyprshare update check|download|apply`).
- **Backup and restore** of the hyprshare state
  (`hyprshare backup create|restore|delete|list|inspect`).
- **Configuration profiles** (`hyprshare profile …`) and full config
  read/write (`hyprshare config show|get|set`).
- **File search** with glob patterns, size filters, and sorting
  (`hyprshare search files`).
- **System monitor** reporting real CPU, memory, disk, network, and connection
  counters read from the kernel (`hyprshare monitor snapshot`).
- **Diagnostics**: doctor health checks with severity levels and remediation
  hints, a security auditor, network diagnostics, and support bundles
  (`hyprshare doctor|check|netdiag|audit|support bundle`).
- **Plugin system**: shared objects exporting `hyprshare_plugin_create` are
  loaded with `dlopen()`, can contribute CLI commands, and stay registered
  across invocations (`hyprshare plugin …`), with a documented ABI and an
  example plugin in `examples/plugins/`.
- **Mobile integration**: a transfer server and a remote-control server
  (`hyprshare mobile|remote`), with Android (Kotlin) and iOS (Swift) client
  sources in `mobile/`.
- **Interactive shell**, batch runner, and span-based profiling
  (`hyprshare shell`, `batch …`, `profiling …`).

### Packaging and tooling

- **CMake install rules** for the binaries, bash/zsh/fish completions, systemd
  user units, and documentation.
- **Two AUR packages**: `hyprshare` (CLI + daemon) and `hyprshare-desktop`
  (GUI, depending on the CLI) in `packaging/arch/`, plus Debian, Fedora, and
  Nix scaffolding.
- **Per-module test binaries** (13 suites) registered with CTest, covering the
  core, TLS, HTTP, cloud, sync, recording, monitor, update, web, daemon,
  service, CLI dispatch, and plugin layers.
- **Documentation**: architecture overview with an implementation status
  table, ADRs, threat model, compatibility matrix, mobile protocol
  specification, API reference, and user guide.
