# hyprshare API Reference

## CLI Commands

`hyprshare --help` lists every command with its full usage, and
`hyprshare <command> --help` shows the usage of a single command.

### Core Commands

| Command | Description |
|---------|-------------|
| `hyprshare status` | Show system status |
| `hyprshare doctor` | Run health diagnostics |
| `hyprshare check` | Quick health check |
| `hyprshare support bundle` | Write a support bundle for bug reports |
| `hyprshare config show` | Show configuration |
| `hyprshare config set <key> <value>` | Set configuration value |
| `hyprshare config get <key>` | Get configuration value |

### Discovery and Compatibility

| Command | Description |
|---------|-------------|
| `hyprshare discovery status` | Show LAN discovery status |
| `hyprshare discovery browse` | Browse discovered devices |
| `hyprshare backend list` | List available sharing backends |
| `hyprshare compat show <app>` | Show the compatibility profile of an app |
| `hyprshare app launch <app>` | Launch an app with the planned sharing backend |

### File Sharing

| Command | Description |
|---------|-------------|
| `hyprshare inbox serve` | Start inbox server |
| `hyprshare inbox put <host:port> <path>` | Send files to inbox |
| `hyprshare inbox receive` | Receive files |
| `hyprshare inbox status` | Show inbox status |
| `hyprshare inbox history` | Show receive history |
| `hyprshare inbox history clear` | Clear receive history |
| `hyprshare inbox open` | Open the configured receive directory |
| `hyprshare inbox announce` | Announce the inbox over mDNS |
| `hyprshare inbox extract <archive>` | Extract a received archive |

### Session Management

| Command | Description |
|---------|-------------|
| `hyprshare session list` | List sessions |
| `hyprshare session show <id>` | Show session details |
| `hyprshare session delete <id>` | Delete a session |
| `hyprshare session prune` | Remove old sessions |
| `hyprshare session verify <id>` | Verify session payload integrity |
| `hyprshare session serve <id>` | Serve session files |
| `hyprshare session fetch <host:port>` | Fetch remote session |
| `hyprshare session inspect <host:port>` | Inspect remote session |
| `hyprshare session export <id>` | Export a session bundle |
| `hyprshare session import <bundle>` | Import a session bundle |
| `hyprshare session receive <id>` | Receive a shared session |

### Peer Management

| Command | Description |
|---------|-------------|
| `hyprshare peer self` | Show device identity |
| `hyprshare peer list` | List trusted peers |
| `hyprshare peer inspect <path>` | Inspect a peer card |
| `hyprshare peer pair <path>` | Pair with a peer card |
| `hyprshare peer invite create` | Create a pairing invitation |
| `hyprshare peer export` | Export peer card |
| `hyprshare peer import <path>` | Import peer card |
| `hyprshare peer add <id>` | Add trusted peer |
| `hyprshare peer remove <id>` | Remove trusted peer |

### Content Sharing

| Command | Description |
|---------|-------------|
| `hyprshare share file <path>` | Share a file |
| `hyprshare share folder <path>` | Share a folder |
| `hyprshare share clipboard` | Share clipboard |
| `hyprshare share screen` | Share screen |
| `hyprshare clipboard read` | Read the clipboard |
| `hyprshare clipboard write` | Write to the clipboard |
| `hyprshare clipboard receive <session-id>` | Receive a clipboard share |

### Sync Services

| Command | Description |
|---------|-------------|
| `hyprshare sync start` | Start file sync |
| `hyprshare sync stop` | Stop file sync |
| `hyprshare sync status` | Show file sync status |
| `hyprshare sync add <path> <device>` | Add sync rule |
| `hyprshare sync remove <rule-id>` | Remove sync rule |
| `hyprshare sync list` | List sync rules |
| `hyprshare clipboard-sync start` | Start clipboard sync |
| `hyprshare clipboard-sync stop` | Stop clipboard sync |
| `hyprshare clipboard-sync status` | Show clipboard sync status |
| `hyprshare clipboard-sync push <text>` | Push to clipboard |
| `hyprshare clipboard-sync pull` | Pull from clipboard |
| `hyprshare clipboard-sync history` | Show clipboard sync history |

### Recording

| Command | Description |
|---------|-------------|
| `hyprshare record start` | Start recording |
| `hyprshare record stop` | Stop recording |
| `hyprshare record pause` | Pause recording |
| `hyprshare record resume` | Resume recording |
| `hyprshare record status` | Show recording status |

### Mobile and Remote Control

| Command | Description |
|---------|-------------|
| `hyprshare mobile serve` | Start the mobile transfer server |
| `hyprshare mobile send <device-id> <file>` | Send a file to a mobile device |
| `hyprshare remote serve [--port <port>]` | Start the remote control server as a detached process |
| `hyprshare remote status [--json]` | Show pid, port, recorded-event count and injection backend |
| `hyprshare remote stop` | Stop the server and clear its pid/port files |

### Networking and TLS

| Command | Description |
|---------|-------------|
| `hyprshare netdiag interfaces` | List network interfaces |
| `hyprshare netdiag test` | Run connectivity checks |
| `hyprshare netdiag latency <host> [port]` | Measure latency to a host |
| `hyprshare netdiag resolve <hostname>` | Resolve a hostname |
| `hyprshare tls generate` | Generate TLS certificate |
| `hyprshare tls verify <cert>` | Verify certificate |
| `hyprshare tls info <cert>` | Show certificate details |
| `hyprshare tls status` | Show TLS status |

### Batch and Shell

| Command | Description |
|---------|-------------|
| `hyprshare batch run-file <file>` | Run commands listed in a file |
| `hyprshare batch run-commands <command>...` | Run the given commands |
| `hyprshare shell` | Start interactive shell |

### System

| Command | Description |
|---------|-------------|
| `hyprshare backup create` | Create backup |
| `hyprshare backup restore <id>` | Restore backup |
| `hyprshare backup delete <id>` | Delete a backup |
| `hyprshare backup list` | List backups |
| `hyprshare backup inspect <id>` | Inspect a backup |
| `hyprshare plugin list` | List registered plugins |
| `hyprshare plugin load <path>` | Load and register a plugin |
| `hyprshare plugin unload <name>` | Unregister a plugin |
| `hyprshare plugin enable <name>` | Enable a registered plugin |
| `hyprshare plugin disable <name>` | Disable a registered plugin |
| `hyprshare plugin commands` | List the commands contributed by enabled plugins |
| `hyprshare monitor start` | Start monitoring |
| `hyprshare monitor stop` | Stop monitoring |
| `hyprshare monitor status` | Show monitoring status |
| `hyprshare monitor snapshot` | Take metrics snapshot |
| `hyprshare audit run` | Run security audit |
| `hyprshare audit check <id>` | Run specific check |
| `hyprshare audit list` | List available audit checks |

### Cloud, Profiles, and Maintenance

| Command | Description |
|---------|-------------|
| `hyprshare cloud configure <provider>` | Configure the cloud provider |
| `hyprshare cloud upload <local> <remote>` | Upload a file to the cloud |
| `hyprshare cloud download <remote> <local>` | Download a file from the cloud |
| `hyprshare cloud sync <local-dir> <remote-dir>` | Sync a directory with the cloud |
| `hyprshare cloud list <remote-dir>` | List remote cloud objects |
| `hyprshare cloud status` | Show cloud sync status |
| `hyprshare profile list` | List configuration profiles |
| `hyprshare profile get <profile-id>` | Show a profile |
| `hyprshare profile create <name>` | Create a profile |
| `hyprshare profile delete <profile-id>` | Delete a profile |
| `hyprshare profile switch <profile-id>` | Switch the active profile |
| `hyprshare profile export <profile-id> <path>` | Export a profile |
| `hyprshare profile import <path>` | Import a profile |
| `hyprshare search files --path <dir>` | Search for files |
| `hyprshare update check` | Check for a newer release |
| `hyprshare update status` | Show the current version |
| `hyprshare daemon status` | Ask the daemon whether it is running |
| `hyprshare daemon stop` | Ask the daemon to stop |
| `hyprshare daemon inbox list` | List the inbox listeners of the daemon |
| `hyprshare daemon inbox start` | Let the daemon receive files into a directory |
| `hyprshare daemon inbox stop <id>` | Stop one inbox listener of the daemon |
| `hyprshare web serve` | Serve the REST API of the web interface |
| `hyprshare web status` | Show how to start the web interface |
| `hyprshare profiling start <name>` | Start a profiling span |
| `hyprshare profiling stop <name>` | Stop a profiling span |
| `hyprshare profiling report` | Show the profiling report |
| `hyprshare profiling clear` | Clear the profiling data |

## Global Options

| Option | Description |
|--------|-------------|
| `--json` | Output in JSON format |
| `--dry-run` | Plan without executing |
| `--stage` | Stage a share for later serving |
| `--tls` | Enable TLS transport |
| `--tls-cert <path>` | TLS certificate file |
| `--tls-key <path>` | TLS key file |
| `--tls-ca <path>` | TLS CA certificate file |
| `--insecure` | Disable certificate verification |
| `--help` | Show help; combine with a command for command-specific usage |

## Library API

### hyprshare::core

- `Config` - Configuration management
- `load_config()` / `save_config()` - Read and write the configuration file
- `get_config_value()` / `set_config_value()` - Read and write single settings
- `BackendRegistry` - Backend discovery
- `AdapterRegistry` - App compatibility profiles
- `SharePlanner` - Screen share planning
- `AppLauncher` - App launch orchestration
- `TrustStore` - Trusted peer persistence

### hyprshare::tls

- `TlsContext` - TLS context wrapper
- `TlsTransportConfig` - TLS configuration
- `generate_self_signed_cert()` - Certificate generation
- `upgrade_client_socket()` - Client TLS upgrade
- `upgrade_server_socket()` - Server TLS upgrade

### hyprshare::sync

- `FileSyncService` - File synchronization. Transfers files to a peer over the
  inbox protocol
- `ClipboardSyncService` - Clipboard synchronization. Polls the system clipboard and
  persists a history; it does not yet exchange content with a remote device

### hyprshare::monitor

- `SystemMonitor` - System metrics collection

### hyprshare::security

- `SecurityAuditor` - Security audit

### hyprshare::backup

- `BackupManager` - Backup and restore

### hyprshare::plugin

- `PluginManager` - Plugin loading and unloading
- `PluginRegistry` - Persistent plugin registrations
- `Plugin` - Interface implemented by a plugin
- `PluginCommand` - A command contributed by a plugin
- `hyprshare_plugin_create` - Symbol a plugin shared object has to export

### hyprshare::perf

- `BufferPool` - Buffer management
- `ProgressTracker` - Progress tracking
- `RateLimiter` - Rate limiting

### hyprshare::daemon

- `DaemonServer` - The Unix socket server of the daemon
- `DaemonState` - The inbox listeners the daemon hosts
- `send_request` - Sends a request to a running daemon over its socket
- `parse_request` / `parse_response` - The protocol of the socket

### hyprshare::web

- `ApiServer` - REST API server. It listens on the loopback address by default,
  because the API exposes the local configuration and the staged sessions
- `parse_multipart` - Reads the files out of a multipart form

The routes the CLI serves:

| Route | Content |
|-------|---------|
| `GET /api/status` | Version and the paths in use |
| `GET /api/health` | The checks of the doctor |
| `GET /api/config` | The settings of the configuration file |
| `GET /api/backends` | The available sharing backends |
| `GET /api/peers` | The trusted peers |
| `GET /api/inbox` | The receive directory and the history size |
| `POST /api/upload` | Stores the files of a multipart form in the inbox |

### hyprshare::shell

- `InteractiveShell` - Interactive shell

### hyprshare::cloud

- `CloudSyncClient` - Cloud upload, download, and synchronization. A provider is
  either `local` (a directory) or `http` (an object store)

### hyprshare::config

- `ConfigValidator` - Configuration validation rules

### hyprshare::error

- `RetryManager` - Retry with exponential backoff

### hyprshare::log

- `Logger` - Leveled logging with file and console output

### hyprshare::mobile

- `MobileTransferServer` - File transfer server for mobile clients

### hyprshare::profile

- `ProfileManager` - Multi-profile configuration management

### hyprshare::profiling

- `Profiler` - Span based performance profiling

### hyprshare::recording

- `ScreenRecorder` - Screen recording. The PipeWire stream negotiates a video
  format and the frames are encoded into MP4, WebM, or Matroska with ffmpeg

### hyprshare::search

- `SearchEngine` - File search with filtering and sorting

### hyprshare::update

- `UpdateChecker` - Release version checking. Reads a release manifest from disk
  or over HTTP
