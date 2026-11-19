# Packaging Notes For Arch / AUR

hyprshare ships as **two independent AUR packages**, so the CLI stays lean and
the GUI is optional:

| Package | Contents | PKGBUILD |
|---------|----------|----------|
| `hyprshare` | `hyprshare` + `hyprshared` binaries, shell completions, systemd user units, docs | `PKGBUILD` |
| `hyprshare-desktop` | Tauri GUI, `.desktop` file, icons; depends on `hyprshare` | `PKGBUILD.desktop` |

## Publishing

Each AUR package lives in its own git repository with the file named exactly
`PKGBUILD`. The PKGBUILDs here are already filled in with the real upstream
URL and tag source; when publishing:

1. Pin the source checksum: after tagging `v<pkgver>` upstream, download the
   tarball, run `sha256sum`, and replace `SKIP` in both `sha256sums`.
2. Copy `PKGBUILD` into the `hyprshare` AUR repo, and `PKGBUILD.desktop` into
   the `hyprshare-desktop` AUR repo (renamed to `PKGBUILD`).
3. Generate `.SRCINFO` in each repo: `makepkg --printsrcinfo > .SRCINFO`.
4. Verify locally with `makepkg` (both PKGBUILDs here were verified against
   the real upstream tarball).
5. Commit and push to the AUR (needs an AUR account with an SSH key).

## Build notes

- The CLI package uses CMake's install rules (`cmake --install`), which install
  the binaries and assets to proper `CMAKE_INSTALL_*` paths; `check()` runs
  the full CTest suite (tests are built as part of `build()`).
- The GUI package builds the web frontend first (`web/dist` is gitignored and
  therefore absent from the source tarball; `npm ci` uses the committed
  lockfile), then runs `cargo build --release --locked` in `desktop/src-tauri`.
  The frontend is embedded into the binary at compile time, and the GUI calls
  the installed `hyprshare` CLI at runtime.
- Linux runtime dependencies for the GUI (verified with `ldd`):
  `webkit2gtk-4.1`, `gtk3`.
