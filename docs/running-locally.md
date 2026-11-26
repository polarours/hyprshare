# Running hyprshare locally

All commands assume you are in the repository root. The four front-ends share
one CLI core; start with the CLI, then open whichever interface you like.

## 0. One-time build

```bash
# CLI + daemon (C++23)
cmake -S . -B build -G Ninja && cmake --build build

# TUI
cargo build --release --manifest-path tui/Cargo.toml

# Web frontend + Tauri desktop GUI (embed the web build into the GUI)
(cd web && npm install && npm run build)
(cd desktop/src-tauri && cargo build --release)
```

Or run the same steps CI runs, for both debug and release:

```bash
bash ci/local-ci.sh          # configure + build + test, debug and release
```

## 1. CLI — the source of truth

```bash
export PATH="$PWD/build:$PATH"

hyprshare --help             # list the 34 commands
hyprshare doctor             # health check of this machine
hyprshare status --json      # machine-readable status
```

Every command supports `--json`, and `<command> --help` shows its usage.

## 2. Quick end-to-end transfer (two terminals or two machines)

Terminal A — stage a file and serve it:

```bash
export PATH="$PWD/build:$PATH"
hyprshare share file README.md            # stages a session, prints its id
hyprshare session serve <session-id> --generate-token
# prints the address and the token
```

Terminal B — fetch it:

```bash
export PATH="$PWD/build:$PATH"
hyprshare session fetch <host:port> --token <token> --to ~/Downloads
hyprshare session verify <session-id>     # check the payload checksum
```

Or use the inbox (receiver listens, sender uploads):

```bash
# A: listen for one upload
hyprshare inbox serve --port 28556 --generate-token

# B: send a file
hyprshare inbox put <host>:28556 file.txt --token <token>
hyprshare inbox history                  # see what arrived
```

## 3. Desktop GUI

```bash
export PATH="$PWD/build:$PATH"            # the GUI calls `hyprshare` by name
./desktop/src-tauri/target/release/hyprshare-desktop
```

Pages: Dashboard, File Transfer, Clipboard, Devices, Sessions, Operations,
Tools, Settings. Light/dark/system themes are in the sidebar.

## 4. Terminal UI

```bash
export PATH="$PWD/build:$PATH"
./tui/target/release/hyprshare-tui
```

Keys: `1`–`6` jump to a page, `Tab` cycles, `r` refresh, `q` quit.

| Page | What it shows |
|---|---|
| `1 Dashboard` | Device identity, live CPU/memory/disk, health checks that need attention, recent transfers |
| `2 Inbox` | Transfer history and receiver control (`s` starts a receiver, `d` clears history) |
| `3 Clipboard` | Clipboard-sync history; `↑↓` selects, `Enter` writes the entry back to the clipboard, `w` writes new text |
| `4 Devices` | Trusted peers with their id and pairing age |
| `5 Sessions` | Staged share sessions with capability, file count and size |
| `6 Capture` | Screen-capture backends and the exact capability each one is blocked on |

The Capture page is the honest answer to "why can I not share my screen yet": it
prints the backend's own missing-capability list (`wayland session`,
`v4l2loopback present`, …) instead of a generic failure.

## 5. Web UI

The web serve binds `127.0.0.1`. If port 8080 is taken, pick another:

```bash
export PATH="$PWD/build:$PATH" HYPRSHARE_WEB_DIR="$PWD/web/dist"
hyprshare web serve --port 8081
# open http://127.0.0.1:8081/
```

The REST API lives alongside the frontend: `/api/status`, `/api/health`,
`/api/config`, `/api/backends`, `/api/peers`, `/api/inbox`,
`POST /api/upload`.

## 6. Daemon (optional, for background inbox listeners)

```bash
export PATH="$PWD/build:$PATH"
hyprshare daemon status
hyprshare daemon inbox start --port 28556 --to ~/Downloads
hyprshare daemon inbox list
hyprshare daemon stop
```

## Troubleshooting

- `error: the daemon is not running` — the daemon is optional; start it with
  `hyprshared &` (or via its systemd user unit) before using `daemon inbox …`.
- A front-end says "desktop app only" — you opened the web UI in a browser;
  write actions need the Tauri desktop app, which shells out to the CLI.
- `wl-paste: closed standard file descriptor` — fixed in this version; if you
  see it, rebuild (the CLI must use the current `process.cpp`).
- Screen capture features need a Wayland session with xdg-desktop-portal and
  PipeWire; `doctor` reports which checks fail on this machine.
