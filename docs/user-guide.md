# hyprshare User Guide

## Getting Started

### Installation

```bash
# Arch Linux
sudo pacman -S cmake ninja openssl sdbus-cpp pipewire avahi

# Build from source
git clone <repo-url>
cd hyprshare
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

### Basic Usage

```bash
# Check system health
hyprshare doctor

# Share a file
hyprshare share file ./document.pdf

# Receive files
hyprshare inbox serve --port 28556

# Send files to another device
hyprshare inbox put 192.168.1.100:28556 ./photo.jpg
```

### Command Help

Every command is listed by the top-level help, and each command can describe
its own usage:

```bash
# List every command
hyprshare --help

# Show the usage of a single command
hyprshare inbox --help
hyprshare search --help
```

## File Sharing

### Sharing Files

```bash
# Share a single file
hyprshare share file ./document.pdf

# Share a folder
hyprshare share folder ./documents

# Share with staging
hyprshare share file ./large-file.zip --stage
hyprshare session list
hyprshare session serve <session-id> --port 28555
```

### Receiving Files

```bash
# Start inbox server
hyprshare inbox serve --port 28556

# Receive with token
hyprshare inbox serve --port 28556 --token my-secret-token

# Check inbox status
hyprshare inbox status

# View receive history
hyprshare inbox history
```

## Device Management

### Peer Management

```bash
# View device identity
hyprshare peer self

# Add a trusted peer
hyprshare peer add debian-laptop-01 --label "Debian Laptop"

# List trusted peers
hyprshare peer list

# Export peer card
hyprshare peer export --to ./my-peer.peer

# Import peer card
hyprshare peer import ./other-peer.peer
```

### Device Discovery

```bash
# Check discovery status
hyprshare discovery status

# Browse available devices
hyprshare discovery browse
```

## Diagnostics and Compatibility

### Health and Support

```bash
# Full health report
hyprshare doctor

# Fail when a warning is found
hyprshare doctor --fail-on warning

# Quick check
hyprshare check

# Collect a support bundle for bug reports
hyprshare support bundle --to ./support-bundle
```

### Backends and App Compatibility

```bash
# List the available sharing backends
hyprshare backend list

# Show what an app needs in order to share a screen
hyprshare compat show wemeet

# Launch an app with the planned backend
hyprshare app launch wemeet --dry-run
```

## Clipboard

```bash
# Read the current clipboard
hyprshare clipboard read

# Write text to the clipboard
hyprshare clipboard write --text "hello from hyprshare"

# Write the contents of a file to the clipboard
hyprshare clipboard write --from-file ./notes.txt

# Receive a clipboard share
hyprshare clipboard receive <session-id>
```

## Sync Services

### File Synchronization

```bash
# Start file sync
hyprshare sync start

# Add sync rule
hyprshare sync add /home/user/documents debian-laptop-01

# Add sync rule that transfers to a peer
hyprshare sync add /home/user/documents debian-laptop-01 \
  --address 192.168.1.50:28556 --token <access-token>

# Transfer the files of a rule right now
hyprshare sync now <rule-id>

# List sync rules
hyprshare sync list

# Stop sync
hyprshare sync stop
```

### Clipboard Synchronization

```bash
# Start clipboard sync
hyprshare clipboard-sync start

# Push text to clipboard
hyprshare clipboard-sync push "Hello from hyprshare"

# Pull from clipboard
hyprshare clipboard-sync pull

# Send the clipboard to a peer
hyprshare clipboard-sync send 192.168.1.50:28556 --token <access-token>

# Fetch the clipboard of a peer
hyprshare clipboard-sync receive 192.168.1.50:28556 --token <access-token>

# View clipboard history
hyprshare clipboard-sync history
```

The clipboard content travels over the inbox channel, as a file named after
the device that sent it. The service also watches the system clipboard and
records every change in a history under
`~/.local/state/hyprshare/clipboard-history`.

## Network Diagnostics

```bash
# List network interfaces
hyprshare netdiag interfaces

# Run connectivity checks
hyprshare netdiag test

# Measure the latency to a host
hyprshare netdiag latency 192.168.1.100 28556

# Resolve a hostname
hyprshare netdiag resolve hyprshare.local
```

## The Daemon

The daemon hosts the inbox listeners of this machine, so that other devices can
send files even when no command is running.

```bash
# Start the daemon (a systemd unit is shipped in data/systemd/)
hyprshared

# Ask it how it is doing
hyprshare daemon status

# Let it receive files into a directory
hyprshare daemon inbox start --host 0.0.0.0 --port 28556 --to ~/Downloads/hyprshare

# List the listeners
hyprshare daemon inbox list

# Stop one of them, or the whole daemon
hyprshare daemon inbox stop 1
hyprshare daemon stop
```

The daemon listens on a Unix socket in the runtime directory, and the command
line talks to it over that socket.

## Screen Recording

```bash
# Record the screen to a file
hyprshare record start --output ~/Videos/capture

# Choose the container
hyprshare record start --output ~/Videos/capture --format webm
hyprshare record start --output ~/Videos/capture --format mkv

# Pause and resume
hyprshare record pause
hyprshare record resume

# Stop and finish the file
hyprshare record stop

# What is going on
hyprshare record status
```

The capture needs the xdg-desktop-portal and PipeWire integration, and the
frames are encoded with ffmpeg. The output path gets the extension of the
chosen container when it does not have one already.

## Security

### TLS Encryption

```bash
# Generate TLS certificate
hyprshare tls generate my-device

# Start server with TLS
hyprshare inbox serve --tls --tls-cert cert.pem --tls-key key.pem

# Connect with TLS
hyprshare inbox put 192.168.1.100:28556 ./file.txt --tls --tls-ca cert.pem
```

### Security Audit

```bash
# Run full security audit
hyprshare audit run

# Run specific check
hyprshare audit check tls_config

# List available checks
hyprshare audit list
```

## System Management

### Backup and Restore

```bash
# Create backup
hyprshare backup create "Before upgrade"

# List backups
hyprshare backup list

# Restore backup
hyprshare backup restore backup-1234567890

# Delete backup
hyprshare backup delete backup-1234567890
```

### Monitoring

```bash
# Start real-time monitoring
hyprshare monitor start

# Take metrics snapshot
hyprshare monitor snapshot

# Check monitor status
hyprshare monitor status
```

### Plugin Management

Plugins are shared objects that add extra commands to the CLI. See
[examples/plugins/README.md](../examples/plugins/README.md) for the plugin ABI.

```bash
# Load a plugin and register it
hyprshare plugin load /path/to/plugin.so

# List the registered plugins
hyprshare plugin list

# Show the commands contributed by enabled plugins
hyprshare plugin commands

# Run a plugin command
hyprshare hello

# Disable a plugin without unregistering it
hyprshare plugin disable my-plugin
hyprshare plugin enable my-plugin

# Remove the registration
hyprshare plugin unload my-plugin
```

Registrations live in `~/.config/hyprshare/plugins.list`, so a loaded plugin
stays registered across invocations.

## Mobile and Remote Control

```bash
# Start the mobile transfer server
hyprshare mobile serve --port 28556

# Send a file to a mobile device
hyprshare mobile send <device-id> ./photo.jpg

# Start the remote control server
hyprshare remote serve --port 28557

# Show remote control status
hyprshare remote status
```

## Cloud Sync and Profiles

```bash
# Configure a cloud provider
hyprshare cloud configure s3

# Configure a provider that stores objects on an HTTP server
hyprshare cloud configure remote --type http --endpoint https://objects.example.com \
  --bucket files --token <access-key>

# Upload and download
hyprshare cloud upload ./photo.jpg photos/photo.jpg
hyprshare cloud download photos/photo.jpg ./downloaded.jpg

# Sync a whole directory
hyprshare cloud sync ./uploads remote/uploads

# Show cloud sync status
hyprshare cloud status
```

A provider is either `local`, which keeps the objects in a directory under
`~/.local/state/hyprshare/cloud/<provider>/`, or `http`, which stores them on
an HTTP server at `<endpoint>/<bucket>/<key>`. The commands print where an
object actually went, so a remote upload is never mistaken for a local copy.
A provider configured in one invocation stays configured for the next one.

```bash
# Manage configuration profiles
hyprshare profile list
hyprshare profile create work --email me@example.com
hyprshare profile switch work
hyprshare profile export work ./work-profile.toml
hyprshare profile import ./work-profile.toml
```

## File Search and Maintenance

```bash
# Search for files
hyprshare search files --path ~/Documents --pattern '*.pdf'

# Sort and filter the results
hyprshare search files --path ~/Documents --sort-by size --order desc

# Check for a newer release
hyprshare update check
hyprshare update status

# Check against a release manifest
hyprshare update check --manifest ./release.conf

# Check against a manifest that is served over HTTP
hyprshare update check --url https://example.com/hyprshare/latest.json

# Download the release
hyprshare update download --url https://example.com/hyprshare/latest.json

# See what installing it would do
hyprshare update apply

# Install it
hyprshare update apply --yes

# Profile a slow operation
hyprshare profiling start transfer
hyprshare profiling stop transfer
hyprshare profiling report
```

The update checker never contacts a release server. It reads a release
manifest, which is a small key/value file:

```
version = "0.7.0"
download_url = "/path/to/hyprshare-0.7.0.tar.gz"
release_notes = "What changed"
```

Point the checker at a manifest with `--manifest <file>` or with the
`HYPRSHARE_RELEASE_MANIFEST` environment variable. Without one, `update check`
says that no release information is available rather than claiming the
installation is up to date.

## Web Interface

The web interface is a React application in `web/` that talks to a REST API.
The API is served by the CLI:

```bash
# Serve the API on the loopback address
hyprshare web serve

# Serve it on another port
hyprshare web serve --port 9090
```

The API exposes the local configuration and the staged sessions, so it stays
on the loopback address unless another one is asked for with `--host`.

The routes are:

| Route | Content |
|-------|---------|
| `GET /api/status` | Version and the paths in use |
| `GET /api/health` | The checks of the doctor |
| `GET /api/config` | The settings of the configuration file |
| `GET /api/backends` | The available sharing backends |
| `GET /api/peers` | The trusted peers |
| `GET /api/inbox` | The receive directory and the history size |
| `POST /api/upload` | Stores the files of a multipart form in the inbox |

To work on the frontend:

```bash
cd web
npm install
npm run dev
```

The development server proxies the API calls to `http://127.0.0.1:8080`, so
the API has to be running.

## Interactive Shell

```bash
# Start interactive shell
hyprshare shell

# In the shell:
hyprshare> status
hyprshare> doctor
hyprshare> inbox serve --port 28556
hyprshare> help
hyprshare> exit
```

## Batch Operations

```bash
# Run commands from file
hyprshare batch run-file commands.txt

# Run multiple commands
hyprshare batch run-commands "status" "doctor" "inbox status"
```

## Shell Completions

```bash
# Bash
source completions/hyprshare.bash

# Zsh
source completions/hyprshare.zsh

# Fish
source completions/hyprshare.fish
```

## Configuration

Configuration file location: `~/.config/hyprshare/config.toml`

```toml
[general]
default_scope = "local-app"
discoverability = "paired-only"
log_level = "info"

[screen]
preferred_backends = ["portal-screencast", "obs-virtual-camera"]
allow_fallback_to_obs = true

[files]
receive_dir = "~/Downloads/hyprshare"
folder_shares_read_only_by_default = true

[security]
require_explicit_clipboard_approval = true
default_session_ttl_minutes = 30
```

## Troubleshooting

### Common Issues

1. **TLS certificate not found**
   ```bash
   hyprshare tls generate my-device
   ```

2. **Discovery not working**
   ```bash
   hyprshare discovery status
   sudo systemctl start avahi-daemon
   ```

3. **Permission denied**
   ```bash
   hyprshare doctor
   # Check file permissions in output
   ```

4. **Sync not starting**
   ```bash
   hyprshare sync status
   hyprshare sync start
   ```
