# Packaging Notes For Arch / AUR

hyprshare ships as **one AUR package**: install `hyprshare-git` and you get all
four entry points from the same build:

| Entry point | Command | What it is |
|-------------|---------|------------|
| CLI | `hyprshare …` | the 34-command CLI, plus the `hyprshared` daemon |
| Terminal UI | `hyprshare-tui` | ratatui dashboard — device, inbox, clipboard |
| Desktop GUI | `hyprshare-desktop` | Tauri window with the six-page macOS-style UI |
| Web UI | `hyprshare web serve` | REST API + the React frontend at `/` |

All front-ends call the same CLI, so they cannot disagree with each other.

## Publishing

**Published**: [`hyprshare-git`](https://aur.archlinux.org/packages/hyprshare-git)
tracks `master`. Its AUR repo is `ssh://aur@aur.archlinux.org/hyprshare-git.git`.

The published `-git` PKGBUILD lives in the AUR repo, not in this directory.
This directory's `PKGBUILD` is the **release-tarball** variant (for a future
stable `hyprshare` package): it downloads `v$pkgver.tar.gz` and pins a
`sha256sums`. To publish that variant instead:

1. Tag `v<pkgver>` upstream, download the tarball, run `sha256sum`, and put the
   hash in `sha256sums`.
2. Copy `PKGBUILD` into a `hyprshare` AUR repo.
3. Generate `.SRCINFO`: `makepkg --printsrcinfo > .SRCINFO`.
4. Verify: `makepkg -s` (builds all four entry points, runs the 13-suite check).
5. Commit and push to AUR (needs an AUR account with an SSH key).

To build the `-git` variant locally, use the PKGBUILD published on AUR: its
`source` is `git+https://github.com/polarours/hyprshare.git`, `sha256sums=('SKIP')`,
and `pkgver()` reads the version from `CMakeLists.txt` plus the git revision.

## Build notes

- C++ CLI + daemon via CMake, with tests built for the `check()` stage.
- Web frontend via `npm ci` + `npm run build` (uses the committed lockfile);
  installed to `/usr/share/hyprshare/web`, which `hyprshare web serve` serves
  together with the REST API.
- GUI and TUI via `cargo build --release --locked` (Cargo.lock committed);
  all front-ends are subprocess callers of the installed `hyprshare`.
- Linux runtime dependencies (verified with `ldd`): `openssl`, `sdbus-cpp`,
  `pipewire`, `webkit2gtk-4.1`, `gtk3`.
