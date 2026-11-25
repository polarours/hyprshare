# Changelog

All notable changes to hyprshare are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

- **`mobile serve` runs as a background service**, matching `remote`, `monitor`
  and `clipboard-sync`. It used to block the terminal forever with no way to
  query or stop it from another process. `mobile serve` now detaches (pid and
  port files under the state dir), `mobile status [--json]` reports the real
  pid/port, and `mobile stop` terminates it; `--foreground` keeps the old
  blocking behaviour. The desktop Services page gained a start/stop control with
  a live status row alongside the existing "send a file" action. Verified end to
  end: `discover` answers on the started port, a second `serve` reports
  `already_running`, and `stop` releases the socket.
- **Launcher integration**: `cmake --install` now installs both `.desktop`
  entries (`hyprshare` for the GUI, `hyprshare (terminal)` for the TUI) and a
  themed icon at 16/24/32/48/64/128/256/512 plus a scalable SVG. Previously
  only the PKGBUILD copied those files, so a plain `cmake --install` (and any
  packaging that relied on it) produced an app that never appeared in a
  launcher. The entry declares `StartupWMClass=hyprshare-desktop` so the window
  is grouped correctly and a single main category (`Network`).
- **New application icon**: a blue-to-teal squircle with a bold bidirectional
  transfer arrow, drawn as SVG and exported to every hicolor size plus ICO/ICNS
  for the Tauri bundle. The old icon was a thin low-contrast node graph that
  turned to mush below 48px.
- **PKGBUILDs (repo and AUR) no longer duplicate the launcher files** — they
  rely on `cmake --install` and add a `post_install`/`post_upgrade` that
  refreshes the desktop database and icon cache.
- **CLI correctness audit: ten commands now honour `--json`** (`cloud configure`,
  `profile create`, `web status`, `batch run-commands`, `sync add`, `monitor
  start/stop`, `profiling start/stop/clear`). Several of them emitted plain text
  even when asked for JSON, which broke the desktop app's output parsing. String
  values are escaped through one shared `json_escape` helper.
- **`monitor` is now a real detached service** instead of blocking the caller
  forever. `start` spawns a sampler that persists readings to the state dir, so
  `status --json` from another process returns the real latest CPU/memory/disk
  snapshot; `stop` terminates it.
- **`profiling` state persists across processes** (a TSV store under the state
  dir), so `start` in one call and `report`/`stop` in another actually work.
- **New `daemon start`**: launches the shipped `hyprshared`, waits for the IPC
  socket to answer, and records a pid file; `status --json` reports
  `running: false` instead of erroring when the daemon is down, and `stop` waits
  for the shutdown to complete.
- **`audit check <id> --json` now includes warnings, skips and the check
  objects** — previously a WARN-only check looked like an empty report.
- **`netdiag latency <host>` resolves hostnames** via `getaddrinfo` instead of
  rejecting anything that was not a literal IP.
- **New desktop "Services" page** covers the six CLI areas that previously had
  no UI at all: mobile send, remote serve/status/stop, cloud configure/upload/
  download, app compatibility, batch commands and profiling, plus daemon
  start/stop.
- **Desktop visual pass**: a subtle background gradient, card borders and hover
  lift, a restrained sidebar active state with an accent bar, gradient primary
  buttons, uppercase group labels aligned to the cards, a real primary button
  for the empty-state action, and per-page inline input rows.
- **The TUI was redesigned.** It now has a branded header bar (version, device,
  ready/refreshing state), a rounded tab strip with per-page counts, metric
  meters with fixed-width bars, table headers, zebra striping, semantic
  fail/warn colours, friendly empty states for every panel, and a per-page key
  hint bar. The clipboard list gained arrow-key selection and Enter to copy an
  entry back, and a refresh no longer blanks the screen.
- **The desktop app finds its CLI again when launched outside a shell.** Actions
  shelled out to `hyprshare` through PATH only, so starting the GUI from a
  launcher (where the developer build directory is not on PATH) made every
  action fail with "failed to run hyprshare". It now resolves, in order:
  `HYPRSHARE_BIN`, next to the app binary, `./build/hyprshare` walking up four
  directories, then `/usr/bin` and `/usr/local/bin`, and the error names what
  was searched.
- **Dashboard polish**: statistic cards put an uppercase label with a status dot
  above a tabular number, health rows tint their icon by severity and use a
  different glyph for failures than warnings, rows got more breathing room, the
  active nav item lost its heavy glow, native scrollbars are restyled, and the
  empty "Trusted devices" row links to the Devices page instead of being a dead
  end.
- **Browser build degrades honestly**: pages that need CLI-only data (file
  transfer status and history) no longer show a red error next to a spinner that
  never finishes; they say the detail is desktop-only and keep the working parts
  usable.
- **`remote status` is no longer hardcoded.** `remote serve` now starts a real
  detached server (pid + port files), `status` probes the pid and reports the
  real pid/port plus a count of input events, and a new `remote stop` terminates
  it and clears the stale state. Both text and JSON output agree on the state.
- **Remote input events are persisted** to `~/.local/state/hyprshare/remote/input.log`
  instead of being printed to a terminal nobody reads. Injection still needs an
  external backend (`ydotool`/`wtype`/`dotool`); `status` reports the detected
  backend and says `none` rather than claiming an injection happened.
- **Fixed two real socket bugs in the remote server found by a live probe**:
  several control messages in one TCP packet used to be dropped (only the first
  line was handled), and a malformed coordinate could throw and kill the
  detached server. Both are covered by the CLI dispatch suite.
- **The mobile server now answers every message type the Android and iOS
  clients send** instead of silently ignoring most of them: `ping`→`pong`,
  `capabilities` (stored per client and echoed with a count), `clipboard_sync`
  (really written through `wl-copy`), and `file_complete` (verifies the
  received file's size and SHA-256, answering `ok`/`mismatch`/`missing`).
  `screen_start`/`screen_stop` and `input_event` answer an explicit
  `unsupported` status, because streaming frames into this socket and injecting
  input (needs `ydotool`/`wtype`) are not implemented — clients can now tell
  "not supported" apart from "no reply".
- **`discover_response` advertises the host's real LAN IPv4** instead of the
  wildcard `0.0.0.0`, which no client can connect to.
- **New `hyprshare_mobile_tests` suite** (14th) drives the protocol over a real
  socket: discovery, auth, keepalive, capabilities, a full file transfer, and
  the size/SHA-256 mismatch paths.
- **Added a GitHub Pages landing page** (`site/`, deployed by `.github/workflows/pages.yml`)
  at `polarours.github.io/hyprshare` — a showcase with the real screenshot
  wall, feature cards and install commands, matching the README style.
- **Published to the AUR as `hyprshare-git`** (tracks `master`): one package
  provides the CLI/daemon, TUI, desktop GUI and web UI. The main README and
  packaging notes now point to it.
- **`clipboard-sync start` now runs a real detached background process** with a
  pid file, so `status`/`stop` work across CLI invocations (the watcher used to
  die with the starting process).
- **Fixed installed data-directory resolution**: the binary used to hardcode
  the build-time source path as its data directory, so an installed package
  looked for backends/adapters in a nonexistent build dir. It now prefers the
  installed data dir (`/usr/share/hyprshare`) when present, falling back to the
  source tree only in development builds.
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
- **New Operations page in the GUI** exposes audit, backups, profiles, plugins,
  recording and update checks — command groups that previously had no front-end
  entry point. All actions go through the CLI.
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
