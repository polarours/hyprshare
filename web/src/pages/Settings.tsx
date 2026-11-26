import { useState, useEffect } from 'react'
import { api, type Backend, runAction, runJson, pickPath, desktopAvailable } from '../api'
import { Icon, type IconName } from '../components/Icon'
import { StatusBanner } from '../components/ui'

// `config show --json` is nested by section, unlike the flat /api/config reply.
interface ConfigShow {
  config_file: string
  general: { default_scope: string; discoverability: string; log_level: string }
  screen: { preferred_backends: string[]; allow_fallback_to_obs: boolean }
  files: { receive_dir: string; folder_shares_read_only_by_default: boolean }
  security: { require_explicit_clipboard_approval: boolean; default_session_ttl_minutes: number }
}

// Every value the CLI accepts for the constrained keys. They mirror
// ConfigValidator::default_validator(), so a choice here is never rejected by
// the backend and a rejection there is always explainable here.
const LOG_LEVELS = ['debug', 'info', 'warning', 'error', 'fatal']
const DISCOVERABILITY = ['paired-only', 'open', 'disabled']

interface FieldProps {
  icon: IconName
  title: string
  sub?: string
  children: React.ReactNode
}

function Field({ icon, title, sub, children }: FieldProps) {
  return (
    <div className="row">
      <div className="row-icon"><Icon name={icon} /></div>
      <div className="row-main">
        <div className="row-title">{title}</div>
        {sub && <div className="row-sub">{sub}</div>}
      </div>
      <div className="row-inline-inputs">{children}</div>
    </div>
  )
}

function Settings() {
  const [config, setConfig] = useState<ConfigShow | null>(null)
  const [backends, setBackends] = useState<Backend[]>([])
  const [error, setError] = useState<string | null>(null)
  const [notice, setNotice] = useState<string | null>(null)
  const [loading, setLoading] = useState(true)
  const [busy, setBusy] = useState<string | null>(null)
  const [receiveDir, setReceiveDir] = useState('')
  const [ttl, setTtl] = useState('30')

  const load = async (cancelled = false) => {
    try {
      const [raw, backendResult] = await Promise.all([
        runJson<ConfigShow>(['config', 'show', '--json'], 0),
        api.backends(),
      ])
      if (cancelled) {
        return
      }
      setConfig(raw)
      setBackends(backendResult.backends)
      setReceiveDir(raw.files?.receive_dir ?? '')
      setTtl(String(raw.security?.default_session_ttl_minutes ?? 30))
      setError(null)
    } catch (cause) {
      if (!cancelled) {
        setError(cause instanceof Error ? cause.message : String(cause))
      }
    } finally {
      if (!cancelled) {
        setLoading(false)
      }
    }
  }

  useEffect(() => {
    let cancelled = false
    load(cancelled)
    return () => {
      cancelled = true
    }
  }, [])

  /** Writes one key and reports what the CLI actually said. */
  const save = async (key: string, value: string, label: string) => {
    setError(null)
    setNotice(null)
    setBusy(key)
    try {
      await runAction(['config', 'set', key, value, '--json'])
      setNotice(`${label} saved`)
      await load()
    } catch (cause) {
      // The CLI rejects invalid values and prints the reason; show it instead of
      // a generic failure so the user knows which value was refused.
      setError(cause instanceof Error ? cause.message : String(cause))
    } finally {
      setBusy(null)
    }
  }

  if (loading) {
    return (
      <div className="loading">
        <div className="spinner" />
        <div>Loading preferences…</div>
      </div>
    )
  }

  if (error && !config) {
    return (
      <div className="loading">
        <div className="row-icon"><Icon name="alert" /></div>
        <p>Could not reach hyprshare.</p>
        <p className="mono">{error}</p>
      </div>
    )
  }

  const editable = desktopAvailable()

  return (
    <>
      <div className="page-eyebrow">Preferences</div>
      <h1 className="page-title">Settings</h1>
      <p className="page-subtitle">
        How this device shares and discovers other peers. Every change is written through
        <code> hyprshare config set</code>.
      </p>

      {!editable && (
        <StatusBanner kind="message">
          Changing config needs the desktop app; in the browser this page is read-only.
        </StatusBanner>
      )}
      {error && <StatusBanner kind="error">{error}</StatusBanner>}
      {notice && <StatusBanner kind="message">{notice}</StatusBanner>}

      {config && (
        <div className="group">
          <div className="group-title">Discovery</div>
          <div className="group-body">
            <Field
              icon="device"
              title="Discoverability"
              sub="Who may see this device on the local network"
            >
              <select
                className="text-input"
                value={config?.general.discoverability}
                disabled={!editable || busy !== null}
                aria-label="Discoverability"
                onChange={e => save('general.discoverability', e.target.value, 'Discoverability')}
              >
                {DISCOVERABILITY.map(value => (
                  <option key={value} value={value}>{value}</option>
                ))}
              </select>
            </Field>
            <Field icon="share" title="Default scope" sub="What a new share exposes">
              <input
                className="text-input"
                defaultValue={config?.general.default_scope}
                disabled={!editable || busy !== null}
                aria-label="Default scope"
                onBlur={e => {
                  const value = e.target.value.trim()
                  if (value && value !== config?.general.default_scope) {
                    save('general.default_scope', value, 'Default scope')
                  }
                }}
              />
            </Field>
          </div>
        </div>
      )}

      {config && (
        <div className="group">
          <div className="group-title">Files</div>
          <div className="group-body">
            <Field icon="folder" title="Receive directory" sub={config?.files.receive_dir}>
              <input
                className="text-input"
                value={receiveDir}
                disabled={!editable || busy !== null}
                aria-label="Receive directory"
                onChange={e => setReceiveDir(e.target.value)}
                onKeyDown={e => {
                  if (e.key === 'Enter') {
                    save('files.receive_dir', receiveDir.trim(), 'Receive directory')
                  }
                }}
              />
              <button
                className="btn btn-secondary"
                disabled={!editable || busy !== null}
                onClick={async () => {
                  const picked = await pickPath('folder')
                  if (!picked) return
                  setReceiveDir(picked)
                  save('files.receive_dir', picked, 'Receive directory')
                }}
              >
                Browse
              </button>
              <button
                className="btn btn-primary"
                disabled={!editable || busy !== null || receiveDir.trim() === config?.files.receive_dir}
                onClick={() => save('files.receive_dir', receiveDir.trim(), 'Receive directory')}
              >
                Save
              </button>
            </Field>
            <Field
              icon="shield"
              title="Folder shares read-only by default"
              sub="New folder shares cannot be written to unless explicitly allowed"
            >
              <input
                type="checkbox"
                checked={config?.files.folder_shares_read_only_by_default}
                disabled={!editable || busy !== null}
                aria-label="Folder shares read-only by default"
                onChange={e =>
                  save(
                    'files.folder_shares_read_only_by_default',
                    e.target.checked ? 'true' : 'false',
                    'Folder read-only default',
                  )
                }
              />
            </Field>
          </div>
        </div>
      )}

      {config && (
        <div className="group">
          <div className="group-title">Security</div>
          <div className="group-body">
            <Field
              icon="clipboard"
              title="Approve clipboard sharing explicitly"
              sub="Ask before a peer can pull clipboard contents"
            >
              <input
                type="checkbox"
                checked={config?.security.require_explicit_clipboard_approval}
                disabled={!editable || busy !== null}
                aria-label="Approve clipboard sharing explicitly"
                onChange={e =>
                  save(
                    'security.require_explicit_clipboard_approval',
                    e.target.checked ? 'true' : 'false',
                    'Clipboard approval',
                  )
                }
              />
            </Field>
            <Field
              icon="clock"
              title="Session lifetime"
              sub={`${config?.security.default_session_ttl_minutes} minutes · allowed 1–1440`}
            >
              <input
                className="text-input"
                type="number"
                min={1}
                max={1440}
                value={ttl}
                disabled={!editable || busy !== null}
                aria-label="Session lifetime in minutes"
                onChange={e => setTtl(e.target.value)}
                onKeyDown={e => {
                  if (e.key === 'Enter') {
                    save('security.default_session_ttl_minutes', ttl.trim(), 'Session lifetime')
                  }
                }}
              />
              <button
                className="btn btn-primary"
                disabled={
                  !editable ||
                  busy !== null ||
                  ttl.trim() === String(config?.security.default_session_ttl_minutes)
                }
                onClick={() =>
                  save('security.default_session_ttl_minutes', ttl.trim(), 'Session lifetime')
                }
              >
                Save
              </button>
            </Field>
          </div>
        </div>
      )}

      {config && (
        <div className="group">
          <div className="group-title">Logging</div>
          <div className="group-body">
            <Field icon="file" title="Log level" sub="How much the daemon writes">
              <select
                className="text-input"
                value={config?.general.log_level}
                disabled={!editable || busy !== null}
                aria-label="Log level"
                onChange={e => save('general.log_level', e.target.value, 'Log level')}
              >
                {LOG_LEVELS.map(value => (
                  <option key={value} value={value}>{value}</option>
                ))}
              </select>
            </Field>
          </div>
        </div>
      )}

      <div className="group">
        <div className="group-title">Capture backends</div>
        <div className="group-body">
          {backends.map(backend => (
            <div key={backend.id} className="backend-row">
              <span className="dot" style={{ background: backend.launch_supported ? 'var(--green)' : 'var(--ink-3)', marginTop: 6 }} />
              <div className="row-main">
                <div className="backend-name">{backend.id}</div>
                <div className="backend-summary">
                  {backend.missing && backend.missing.length > 0
                    ? `needs ${backend.missing.join(', ').replace(/_/g, ' ')}`
                    : backend.summary}
                </div>
              </div>
              <span className={`badge ${backend.optional ? '' : 'good'}`}>
                {backend.optional ? 'Optional' : 'Primary'}
              </span>
            </div>
          ))}
        </div>
      </div>
    </>
  )
}

export default Settings
