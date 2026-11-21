import { useState, useEffect } from 'react'
import { useNavigate } from 'react-router-dom'
import { runJson, desktopAvailable } from '../api'
import { Icon } from '../components/Icon'
import { Group, Row, StatusBanner, ActionButton } from '../components/ui'

interface SelfIdentity {
  device_id: string
  device_label: string
  created_at_unix: number
}
interface StatusInfo {
  version: string
  paths: { data_dir: string; config_file: string; state_dir: string }
  catalogs: { backends: number; adapters: number }
  facts: Record<string, boolean>
}
interface ConfigInfo {
  config_file: string
  general: { default_scope: string; discoverability: string; log_level: string }
  screen: { preferred_backends: string[]; allow_fallback_to_obs: boolean }
  files: { receive_dir: string; folder_shares_read_only_by_default: boolean }
  security: { require_explicit_clipboard_approval: boolean; default_session_ttl_minutes: number }
}
interface Snapshot {
  cpu: number
  memory: number
  disk: number
  network_sent: number
  network_recv: number
  connections: number
}

// Human-friendly labels for the raw doctor fact keys.
const FACT_LABELS: Record<string, string> = {
  hyprland_desktop: 'Hyprland desktop',
  wayland_session: 'Wayland session',
  xwaylandvideobridge_available: 'XWayland video bridge',
  pipewire_active: 'PipeWire',
  wireplumber_active: 'WirePlumber',
  xdg_desktop_portal_active: 'Desktop portal',
  xdph_active: 'xdg-desktop-portal-hyprland',
  obs_available: 'OBS Studio',
  v4l2loopback_present: 'v4l2loopback module',
  avahi_daemon_active: 'Avahi daemon',
  avahi_browse_available: 'mDNS browsing',
  avahi_publish_service_available: 'mDNS publishing',
  discovery_announce_ready: 'Discovery announce',
  discovery_browse_ready: 'Discovery browse',
  hyprctl_available: 'hyprctl',
  systemctl_available: 'systemctl',
  tar_available: 'tar',
  sha256sum_available: 'sha256sum',
  config_loaded: 'Config loaded',
  config_dir_present: 'Config directory',
  data_dir_present: 'Data directory',
  receive_dir_ready: 'Receive directory',
  receive_dir_resolved: 'Receive path resolved',
}

function yesNo(value: boolean) {
  return value ? 'Available' : 'Unavailable'
}

function DeviceDetail() {
  const navigate = useNavigate()
  const [self, setSelf] = useState<SelfIdentity | null>(null)
  const [status, setStatus] = useState<StatusInfo | null>(null)
  const [config, setConfig] = useState<ConfigInfo | null>(null)
  const [snapshot, setSnapshot] = useState<Snapshot | null>(null)
  const [error, setError] = useState<string | null>(null)
  const [loading, setLoading] = useState(true)

  const load = async () => {
    setLoading(true)
    setError(null)
    try {
      const [selfData, statusData, configData, snapshotData] = await Promise.all([
        runJson<SelfIdentity>(['peer', 'self', '--json']),
        runJson<StatusInfo>(['status', '--json']),
        runJson<ConfigInfo>(['config', 'show', '--json']),
        runJson<Snapshot>(['monitor', 'snapshot', '--json'])
      ])
      setSelf(selfData)
      setStatus(statusData)
      setConfig(configData)
      setSnapshot(snapshotData)
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
    } finally {
      setLoading(false)
    }
  }

  const refreshMetrics = async () => {
    setError(null)
    try {
      setSnapshot(await runJson<Snapshot>(['monitor', 'snapshot', '--json']))
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
    }
  }

  useEffect(() => {
    load()
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  return (
    <>
      <button className="back-button" onClick={() => navigate('/')}>
        <Icon name="back" /> Back to Dashboard
      </button>
      <div className="page-eyebrow">This device</div>
      <h1 className="page-title">{self?.device_label || 'Device details'}</h1>
      <p className="page-subtitle">Identity, configuration and live system information for this machine.</p>

      {!desktopAvailable() && (
        <StatusBanner kind="message">Device details require the desktop app.</StatusBanner>
      )}
      {error && <StatusBanner kind="error">{error}</StatusBanner>}
      {loading && <div className="loading"><div className="spinner" /><div>Reading device information…</div></div>}

      {!loading && (
        <>
          <Group title="Identity">
            {self && (
              <>
                <Row icon="device" title={self.device_label} sub="Device name" />
                <Row
                  icon="shield"
                  title="Device ID"
                  sub="Used for peer trust and invitations"
                  value={<span className="mono">{self.device_id}</span>}
                />
                <Row
                  icon="clock"
                  title="Created"
                  sub={new Date(self.created_at_unix * 1000).toLocaleString()}
                />
              </>
            )}
            {status && (
              <Row
                icon="grid"
                title={`hyprshare ${status.version}`}
                sub={`${status.catalogs.backends} backends · ${status.catalogs.adapters} adapters`}
                right={<span className="badge good"><span className="dot pass" />CLI online</span>}
              />
            )}
          </Group>

          {status && (
            <Group title="Paths">
              <Row icon="folder" title="Config file" sub={status.paths.config_file} />
              <Row icon="folder" title="Data directory" sub={status.paths.data_dir} />
              <Row icon="folder" title="State directory" sub={status.paths.state_dir} />
            </Group>
          )}

          {config && (
            <>
              <Group title="Configuration · General">
                <Row icon="settings" title="Default scope" value={config.general.default_scope} />
                <Row icon="shield" title="Discoverability" value={config.general.discoverability} />
                <Row icon="tools" title="Log level" value={config.general.log_level} />
              </Group>
              <Group title="Configuration · Screen & Files">
                <Row
                  icon="record"
                  title="Preferred backends"
                  sub={config.screen.preferred_backends.join(' · ')}
                />
                <Row
                  icon="copy"
                  title="Fallback to OBS"
                  value={config.screen.allow_fallback_to_obs ? 'Enabled' : 'Disabled'}
                />
                <Row icon="folder" title="Receive directory" sub={config.files.receive_dir} />
                <Row
                  icon="shield"
                  title="Folder shares read-only"
                  value={config.files.folder_shares_read_only_by_default ? 'Yes' : 'No'}
                />
              </Group>
              <Group title="Configuration · Security">
                <Row
                  icon="shield"
                  title="Clipboard approval required"
                  value={config.security.require_explicit_clipboard_approval ? 'Yes' : 'No'}
                />
                <Row
                  icon="clock"
                  title="Session lifetime"
                  value={`${config.security.default_session_ttl_minutes} minutes`}
                />
              </Group>
            </>
          )}

          {snapshot && (
            <Group title="Live resources">
              <div className="stat-grid" style={{ marginBottom: 0 }}>
                <div className="stat"><div className="stat-num">{snapshot.cpu.toFixed(1)}%</div><div className="stat-label">CPU</div></div>
                <div className="stat"><div className="stat-num">{snapshot.memory.toFixed(1)}%</div><div className="stat-label">Memory</div></div>
                <div className="stat"><div className="stat-num">{snapshot.disk.toFixed(1)}%</div><div className="stat-label">Disk</div></div>
                <div className="stat"><div className="stat-num">{snapshot.connections}</div><div className="stat-label">Connections</div></div>
              </div>
              <Row
                icon="network"
                title="Network traffic"
                sub={`${(snapshot.network_sent / 1048576).toFixed(1)} MB sent · ${(snapshot.network_recv / 1048576).toFixed(1)} MB received`}
                right={<ActionButton variant="secondary" icon="refresh" onClick={refreshMetrics}>Refresh</ActionButton>}
              />
            </Group>
          )}

          {status && (
            <Group title="Environment">
              {Object.entries(status.facts).map(([key, value]) => (
                <Row
                  key={key}
                  icon={value ? 'check' : 'alert'}
                  title={FACT_LABELS[key] || key}
                  value={
                    <span className={`badge ${value ? 'good' : ''}`}>
                      <span className={`dot ${value ? 'pass' : 'warn'}`} />
                      {yesNo(value)}
                    </span>
                  }
                />
              ))}
            </Group>
          )}
        </>
      )}
    </>
  )
}

export default DeviceDetail
