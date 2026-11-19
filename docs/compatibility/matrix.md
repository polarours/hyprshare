# Compatibility Matrix

## Support Tiers

- `primary`: preferred and expected to work in the intended environment.
- `supported`: maintained path that may depend on optional components.
- `best_effort`: useful fallback with known variability.
- `experimental`: intentionally unstable and not yet a support commitment.

## Current Shipped Profiles

| Profile | Main Intent | Primary Path | Fallbacks |
|---|---|---|---|
| `com.tencent.wemeet` | Tencent Meeting / WeMeet | `portal-screencast` | `obs-virtual-camera` |
| `browser-webrtc` | Browser-based meetings | `portal-screencast` | `obs-virtual-camera` |
| `us.zoom.Zoom` | Zoom | `portal-screencast` | `xwayland-video-bridge`, `obs-virtual-camera` |

## Current Backend Inventory

| Backend | Tier | Purpose |
|---|---|---|
| `portal-screencast` | `primary` | Native Hyprland and Wayland screen sharing via portals and PipeWire |
| `obs-virtual-camera` | `supported` | Compatibility bridge for apps that can consume camera input more reliably than direct capture |
| `xwayland-video-bridge` | `best_effort` | XWayland compatibility path for specific clients |

## Evidence Policy

New compatibility claims should include:

- App version or packaging mode if known.
- Desktop environment and compositor details.
- Diagnostic output from `hyprshare doctor`.
- Clear statement of whether the path is `primary`, `supported`, `best_effort`, or `experimental`.

