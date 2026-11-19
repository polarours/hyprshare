# hyprshare

`hyprshare` is a Linux-native sharing platform for Hyprland and Wayland. It
unifies file transfer, clipboard exchange, screen capture, peer trust, and
system diagnostics behind one CLI-first architecture — plus a native desktop
GUI on top of the same CLI.

## Features

- **File inbox and share sessions** — stage a file or folder, serve it with a
  token and a lifetime, fetch from the peer side; HTTP inbox with checksums,
  extraction, and transfer history
- **Peer identity and trust** — per-device identity, trusted peers, offline
  invitations and pairing, mDNS/Avahi discovery
- **TLS/mTLS transport** — OpenSSL with self-signed X.509 v3 certificates
- **Clipboard** — read/write plus cross-device sync with persistent history
- **Screen capture and recording** — xdg-desktop-portal + PipeWire, OBS
  fallback; MP4/WebM/MKV encoding via ffmpeg
- **34 top-level commands**: sync, cloud, backup, search, monitor, netdiag,
  tls, plugin, batch, daemon, update, profile, and more
- **Desktop GUI** (`hyprshare-desktop`, Tauri): six macOS-style pages with
  light/dark/system themes; every action is a pass-through to the CLI
- **Web interface** — REST API + React frontend
- **Plugin system** — shared objects contributing CLI commands

An implementation status table lives in
[docs/architecture/overview.md](docs/architecture/overview.md).

## Build

Requirements: CMake ≥ 3.28, Ninja, a C++23 compiler, OpenSSL, sdbus-c++,
PipeWire headers. The GUI additionally needs Rust, Node.js/npm.

```bash
# 1. CLI + daemon
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure   # 13 test suites

# 2. Desktop GUI (builds the web frontend, then embeds it)
cd web && npm install && npm run build && cd ..
cd desktop/src-tauri && cargo build --release
```

## Run

```bash
./build/hyprshare doctor                       # health check

# GUI (needs the CLI on PATH)
export PATH="$PWD/build:$PATH"
./desktop/src-tauri/target/release/hyprshare-desktop

# End-to-end file transfer between two machines
./build/hyprshare share file photo.jpg --stage          # machine A
./build/hyprshare session serve <id> --port 29111 --generate-token
./build/hyprshare session fetch <host:port> --token <t> --to ~/recv   # machine B

# HTTP inbox
./build/hyprshare inbox serve --port 8888 --to ~/Downloads --generate-token
./build/hyprshare inbox put <host:8888> file.jpg --token <t>
```

See the full command surface with `hyprshare --help`, and per-command usage
with `hyprshare <command> --help`.

## Architecture

- `src/lib/<module>/` — one static library per concern (core, tls, portal,
  daemon, cloud, sync, recording, diagnostics, …), public headers in
  `include/hyprshare/`
- `src/bin/hyprshare/` — the CLI (command registry + one handler per command)
- `src/bin/hyprshared/` — the daemon binary (Unix socket IPC)
- `desktop/` — the Tauri GUI; `web/` — the React frontend shared by GUI and
  web serve; `mobile/` — Android (Kotlin) and iOS (Swift) client sources
- `tests/unit/` — per-module test binaries registered with CTest

Decisions are recorded as ADRs in [docs/adr/](docs/adr/README.md).

## Installation

Arch Linux: two AUR packages, `hyprshare` (CLI + daemon) and
`hyprshare-desktop` (GUI), defined in
[packaging/arch/](packaging/arch/README.md). Debian, Fedora, and Nix
scaffolding is provided under `packaging/`.

## Documentation

- [Architecture Overview](docs/architecture/overview.md)
- [User Guide](docs/user-guide.md) · [API Reference](docs/api-reference.md)
- [Threat Model](docs/threat-model/threat-model.md) ·
  [Compatibility Matrix](docs/compatibility/matrix.md)
- [Mobile Protocol](docs/protocol/mobile.md)
- [Changelog](CHANGELOG.md)

## License

GPL-3.0-only, see [LICENSE](LICENSE).
