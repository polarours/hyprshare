# hyprshare

`hyprshare` is a Linux-native sharing platform for Hyprland and Wayland. It
unifies file transfer, clipboard exchange, screen capture, peer trust, and
system diagnostics behind one CLI-first architecture. One install gives you
four front-ends over the same CLI: the `hyprshare` command, the `hyprshare-tui`
terminal UI, the `hyprshare-desktop` GUI, and the `hyprshare web serve` web UI.

## Front-ends

- **CLI** — `hyprshare --help`, 34 top-level commands
- **TUI** — `hyprshare-tui`: a ratatui dashboard (device, inbox, clipboard;
  Tab/1-3 to switch, r to refresh, w to write the clipboard, q to quit)
- **Desktop GUI** — `hyprshare-desktop`: a macOS-style Tauri window with six
  pages and light/dark/system themes
- **Web UI** — `hyprshare web serve`: REST API plus the React frontend at
  `http://127.0.0.1:8080/` (override the frontend location with
  `HYPRSHARE_WEB_DIR`)

Every front-end is a subprocess caller of the CLI, so they cannot disagree.

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
# Build CLI + daemon
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure   # 13 test suites

# Build TUI (cargo)
cargo build --release --manifest-path tui/Cargo.toml

# Build desktop GUI (builds the web frontend, then embeds it)
cd web && npm install && npm run build && cd ..
cd desktop/src-tauri && cargo build --release
```

## Run

```bash
./build/hyprshare doctor                       # health check

# GUI (needs the CLI on PATH)
export PATH="$PWD/build:$PATH"
./tui/target/release/hyprshare-tui             # terminal UI
./desktop/src-tauri/target/release/hyprshare-desktop   # desktop GUI
./build/hyprshare web serve --port 8080        # web UI: open http://127.0.0.1:8080/

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

Arch Linux: one AUR package, `hyprshare`, provides the CLI/daemon, TUI,
desktop GUI and web UI; see [packaging/arch/](packaging/arch/README.md).

## Documentation

- [Architecture Overview](docs/architecture/overview.md)
- [User Guide](docs/user-guide.md) · [API Reference](docs/api-reference.md)
- [Threat Model](docs/threat-model/threat-model.md) ·
  [Compatibility Matrix](docs/compatibility/matrix.md)
- [Mobile Protocol](docs/protocol/mobile.md)
- [Changelog](CHANGELOG.md)

## License

GPL-3.0-only, see [LICENSE](LICENSE).
