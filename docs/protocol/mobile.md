# Mobile Communication Protocol

## Overview

This document specifies the protocol for communication between hyprshare desktop and mobile clients (Android/iOS).

## Transport Layer

- **Primary**: TCP with TLS encryption
- **Discovery**: mDNS/Avahi (same as desktop-to-desktop)
- **Port**: 28556 (configurable)

## Message Format

All messages use newline-delimited JSON:

```json
{"type":"message_type","payload":{...}}\n
```

## Server implementation status

The desktop server (`hyprshare mobile serve`) implements these messages today.
Anything marked *unsupported* is answered with an explicit status instead of
being dropped, so a client can tell the difference between "not implemented"
and "no reply yet".

| Message | Direction | Server behavior |
|---|---|---|
| `discover` / `discover_response` | both | Answered with the host's real LAN IPv4 and the listening port |
| `auth_request` / `auth_response` | both | Client registered; the response carries `status` only (no session token yet) |
| `capabilities` / `capabilities_ack` | both | Stored per client and echoed back with a count |
| `ping` / `pong` | both | Answered with a millisecond timestamp |
| `file_offer` / `file_accept` | both | Accepted; the transfer is written into `~/.local/share/hyprshare/inbox` |
| `file_data` | mobile → desktop | A preceding JSON header line is stripped so it never reaches the file |
| `file_complete` / `file_complete_ack` | both | Verifies the received file's size and SHA-256; answers `ok`, `mismatch`, `missing` or `error` |
| `clipboard_sync` / `clipboard_ack` | both | Writes the text through `wl-copy`; answers `error` with a reason when that fails |
| `screen_start` / `screen_stop` / `screen_frame` | both | **Unsupported** — answered with `screen_status: unsupported` |
| `input_event` / `cursor_update` | mobile → desktop | **Unsupported** — answered with `input_ack: unsupported` (needs `ydotool` or `wtype`) |

`hyprshare_mobile_tests` exercises this contract over a real socket, so the
table above is regression-tested rather than aspirational.

## Message Types

### Discovery

#### `discover`
Mobile → Desktop: Request device discovery
```json
{"type":"discover","payload":{"protocol_version":"1.0"}}
```

#### `discover_response`
Desktop → Mobile: Available services
```json
{"type":"discover_response","payload":{"services":[{"name":"hyprshare","host":"192.168.1.100","port":28556,"capabilities":["file_transfer","clipboard_sync"]}]}}
```

`host` is the host's real LAN IPv4 address (never the wildcard `0.0.0.0`), so a
client can connect straight from the phone.

### Authentication

#### `auth_request`
Mobile → Desktop: Authentication request
```json
{"type":"auth_request","payload":{"device_id":"abc123","device_name":"Pixel 7","cert_fingerprint":"sha256:..."}}
```

#### `auth_response`
Desktop → Mobile: Authentication result
```json
{"type":"auth_response","payload":{"status":"ok"}}
```

`status` is the only field sent today; `session_token` / `expires_in` are not
implemented yet.

### File Transfer

#### `file_offer`
Sender → Receiver: File transfer offer
```json
{"type":"file_offer","payload":{"file_id":"f123","name":"photo.jpg","size":1048576,"mime_type":"image/jpeg","sha256":"..."}}
```

#### `file_accept`
Receiver → Sender: Accept transfer
```json
{"type":"file_accept","payload":{"file_id":"f123"}}
```

#### `file_data`
Sender → Receiver: File chunk (binary)
```
{"type":"file_data","payload":{"file_id":"f123","offset":0,"size":65536}}\n
[binary data]
```

#### `file_complete`
Either → Either: Transfer complete
```json
{"type":"file_complete","payload":{"file_id":"f123","status":"ok"}}
```

### Screen Share

#### `screen_start`
Mobile → Desktop: Start screen viewing
```json
{"type":"screen_start","payload":{"quality":"medium","fps":30}}
```

#### `screen_frame`
Desktop → Mobile: Video frame
```
{"type":"screen_frame","payload":{"frame_id":1,"timestamp":1234567890,"width":1920,"height":1080,"format":"h264"}}\n
[binary frame data]
```

#### `screen_stop`
Either → Either: Stop screen sharing
```json
{"type":"screen_stop","payload":{}}
```

### Remote Control

#### `input_event`
Mobile → Desktop: Input event
```json
{"type":"input_event","payload":{"event_type":"touch","x":500,"y":300,"action":"tap"}}
```

Supported event types:
- `touch`: Touch events (tap, swipe, pinch)
- `keyboard`: Keyboard input
- `gesture`: System gestures (back, home, recent)

#### `cursor_update`
Desktop → Mobile: Cursor position update
```json
{"type":"cursor_update","payload":{"x":500,"y":300,"visible":true}}
```

### Session Management

#### `ping` / `pong`
Keep-alive messages
```json
{"type":"ping","payload":{"timestamp":1234567890}}
```

#### `error`
Error response
```json
{"type":"error","payload":{"code":"INVALID_AUTH","message":"Authentication failed"}}
```

## Capability Negotiation

During connection, clients exchange capabilities:

```json
{"type":"capabilities","payload":{"file_transfer":true,"screen_share":true,"remote_control":true,"clipboard":true}}
```

## Security

1. **TLS**: All connections use TLS 1.3
2. **mTLS**: Device certificates for mutual authentication
3. **Token**: Session tokens for authorization
4. **Fingerprint**: Certificate fingerprint verification

## Flow Diagrams

### File Transfer Flow
```
Mobile                    Desktop
  |                          |
  |--- discover ------------>|
  |<-- discover_response ----|
  |                          |
  |--- auth_request -------->|
  |<-- auth_response --------|
  |                          |
  |--- file_offer ---------->|
  |<-- file_accept ----------|
  |                          |
  |--- file_data (chunk) --->|
  |--- file_data (chunk) --->|
  |--- file_complete ------->|
  |<-- file_complete --------|
```

### Remote Control Flow
```
Mobile                    Desktop
  |                          |
  |--- auth_request -------->|
  |<-- auth_response --------|
  |                          |
  |--- screen_start -------->|
  |<-- screen_frame ---------|
  |<-- screen_frame ---------|
  |                          |
  |--- input_event --------->|
  |--- input_event --------->|
  |                          |
  |--- screen_stop --------->|
```
