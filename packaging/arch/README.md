# Packaging Notes For Arch / AUR

hyprshare ships as **one AUR package**: install `hyprshare` and you get all
four entry points from the same build:

| Entry point | Command | What it is |
|-------------|---------|------------|
| CLI | `hyprshare …` | the 34-command CLI, plus the `hyprshared` daemon |
| Terminal UI | `hyprshare-tui` | ratatui dashboard — device, inbox, clipboard |
| Desktop GUI | `hyprshare-desktop` | Tauri window with the six-page macOS-style UI |
| Web UI | `hyprshare web serve` | REST API + the React frontend at `/` |

All front-ends call the same CLI, so they cannot disagree with each other.

## Publishing

The PKGBUILD here is filled in with the real upstream URL and tag source.
When publishing:

1. After tagging `v<pkgver>` upstream, download the tarball, run `sha256sum`,
   and replace `SKIP` in `sha256sums`.
2. Copy `PKGBUILD` into the `hyprshare` AUR repo (as `PKGBUILD`).
3. Generate `.SRCINFO`: `makepkg --printsrcinfo > .SRCINFO`.
4. Verify locally with `makepkg` (it builds all four entry points and runs
   the 13-suite CTest check).
5. Commit and push to the AUR (needs an AUR account with an SSH key).

## Build notes

- C++ CLI + daemon via CMake, with tests built for the `check()` stage.
- Web frontend via `npm ci` + `npm run build` (uses the committed lockfile);
  installed to `/usr/share/hyprshare/web`, which `hyprshare web serve` serves
  together with the REST API.
- GUI and TUI via `cargo build --release --locked` (Cargo.lock committed);
  all front-ends are subprocess callers of the installed `hyprshare`.
- Linux runtime dependencies (verified with `ldd`): `openssl`, `sdbus-cpp`,
  `pipewire`, `webkit2gtk-4.1`, `gtk3`.
