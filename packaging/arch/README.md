# Packaging Notes For Arch / AUR

hyprshare ships as **two independent AUR packages**, so the CLI stays lean and
the GUI is optional:

| Package | Contents | PKGBUILD |
|---------|----------|----------|
| `hyprshare` | `hyprshare` + `hyprshared` binaries, shell completions, systemd user units, docs | `PKGBUILD` |
| `hyprshare-desktop` | Tauri GUI, `.desktop` file, icons; depends on `hyprshare` | `PKGBUILD.desktop` |

## Publishing

Each AUR package lives in its own git repository with the file named exactly
`PKGBUILD`. When publishing:

1. Copy `PKGBUILD` into the `hyprshare` AUR repo, and `PKGBUILD.desktop` into
   the `hyprshare-desktop` AUR repo (renamed to `PKGBUILD`).
2. Replace the placeholders: `Maintainer`, `url`, and the source line's
   release tarball URL. Both packages build from the same source tarball
   `hyprshare-<pkgver>.tar.gz`.
3. Generate `.SRCINFO` in each repo: `makepkg --printsrcinfo > .SRCINFO`.
4. Verify locally with `makepkg -si` (or at least `makepkg --syncdeps`).

## Build notes

- The CLI package uses CMake's install rules (`cmake --install`), which install
  the binaries and assets to proper `CMAKE_INSTALL_*` paths. `BUILD_TESTING=OFF`
  keeps the release build lean; `check()` reconfigures with tests on.
- The GUI package builds the web frontend first (`web/dist` is gitignored and
  therefore absent from the source tarball), then runs `cargo build --release`
  in `desktop/src-tauri`. The frontend is embedded into the binary at compile
  time, and the GUI calls the installed `hyprshare` CLI at runtime.
- Linux runtime dependencies for the GUI: `webkit2gtk-4.1`, `gtk3`,
  `libayatana-appindicator`.
