import { useState, useEffect } from 'react'
import { api, type Backend, type Configuration } from '../api'
import { Icon } from '../components/Icon'

function Settings() {
  const [settings, setSettings] = useState<Configuration | null>(null)
  const [backends, setBackends] = useState<Backend[]>([])
  const [error, setError] = useState<string | null>(null)
  const [loading, setLoading] = useState(true)

  useEffect(() => {
    let cancelled = false

    const load = async () => {
      try {
        const [configResult, backendResult] = await Promise.all([api.config(), api.backends()])
        if (cancelled) {
          return
        }
        setSettings(configResult)
        setBackends(backendResult.backends)
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

    load()
    return () => {
      cancelled = true
    }
  }, [])

  if (loading) {
    return (
      <div className="loading">
        <div className="spinner" />
        <div>Loading preferences…</div>
      </div>
    )
  }

  if (error) {
    return (
      <div className="loading">
        <div className="row-icon"><Icon name="alert" /></div>
        <p>Could not reach hyprshare.</p>
        <p className="mono">{error}</p>
      </div>
    )
  }

  return (
    <>
      <div className="page-eyebrow">Preferences</div>
      <h1 className="page-title">Settings</h1>
      <p className="page-subtitle">How this device shares and discovers other peers.</p>

      {settings && (
        <div className="group">
          <div className="group-title">General</div>
          <div className="group-body">
            <div className="row">
              <div className="row-icon"><Icon name="share" /></div>
              <div className="row-main"><div className="row-title">Default scope</div></div>
              <div className="row-value">{settings.default_scope}</div>
            </div>
            <div className="row">
              <div className="row-icon"><Icon name="device" /></div>
              <div className="row-main"><div className="row-title">Discoverability</div></div>
              <div className="row-value">{settings.discoverability}</div>
            </div>
            <div className="row">
              <div className="row-icon"><Icon name="clock" /></div>
              <div className="row-main"><div className="row-title">Session lifetime</div></div>
              <div className="row-value">{settings.default_session_ttl_minutes} minutes</div>
            </div>
          </div>
        </div>
      )}

      {settings && (
        <div className="group">
          <div className="group-title">Files</div>
          <div className="group-body">
            <div className="row">
              <div className="row-icon"><Icon name="folder" /></div>
              <div className="row-main">
                <div className="row-title">Receive directory</div>
                <div className="row-sub">{settings.receive_dir}</div>
              </div>
            </div>
            <div className="row">
              <div className="row-icon"><Icon name="settings" /></div>
              <div className="row-main"><div className="row-title">Log level</div></div>
              <div className="row-value">{settings.log_level}</div>
            </div>
          </div>
          <div className="hint-text">
            Settings come from the config file. Change them with{' '}
            <code>hyprshare config set &lt;key&gt; &lt;value&gt;</code>.
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
                <div className="backend-summary">{backend.summary}</div>
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
