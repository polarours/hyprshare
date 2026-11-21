import { useState, useEffect } from 'react'
import { Link } from 'react-router-dom'
import { api, runJson, type Health, type Peer, type Status } from '../api'
import { Icon } from '../components/Icon'

function Dashboard() {
  const [status, setStatus] = useState<Status | null>(null)
  const [health, setHealth] = useState<Health | null>(null)
  const [peers, setPeers] = useState<Peer[]>([])
  const [daemonOnline, setDaemonOnline] = useState<boolean | null>(null)
  const [error, setError] = useState<string | null>(null)
  const [loading, setLoading] = useState(true)

  useEffect(() => {
    let cancelled = false

    const load = async () => {
      try {
        const [statusResult, healthResult, peerResult] = await Promise.all([
          api.status(),
          api.health(),
          api.peers()
        ])
        if (cancelled) {
          return
        }
        setStatus(statusResult)
        setHealth(healthResult)
        setPeers(peerResult.peers)
        setError(null)
        try {
          await runJson(['daemon', 'status', '--json'], 0)
          if (!cancelled) setDaemonOnline(true)
        } catch {
          if (!cancelled) setDaemonOnline(false)
        }
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
        <div>Loading your workspace…</div>
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
      <div className="page-eyebrow">Overview</div>
      <h1 className="page-title">Dashboard</h1>
      <p className="page-subtitle">Your devices and the health of this machine at a glance.</p>

      {health && (
        <div className="stat-grid">
          <div className="stat">
            <div className="stat-num" style={{ color: 'var(--green)' }}>{health.pass}</div>
            <div className="stat-label">Passing</div>
          </div>
          <div className="stat">
            <div className="stat-num" style={{ color: health.fail ? 'var(--red)' : 'var(--ink)' }}>{health.fail}</div>
            <div className="stat-label">Failing</div>
          </div>
          <div className="stat">
            <div className="stat-num" style={{ color: health.warn ? 'var(--orange)' : 'var(--ink)' }}>{health.warn}</div>
            <div className="stat-label">Warnings</div>
          </div>
          <div className="stat">
            <div className="stat-num">{peers.length}</div>
            <div className="stat-label">Devices</div>
          </div>
        </div>
      )}

      {status && (
        <div className="group">
          <div className="group-title">This device</div>
          <Link to="/this-device" className="group-body link-card">
            <div className="row">
              <div className="row-icon"><Icon name="device" /></div>
              <div className="row-main">
                <div className="row-title">{status.version ? `hyprshare ${status.version}` : 'hyprshare'}</div>
                <div className="row-sub">{status.config_file}</div>
              </div>
              {daemonOnline === true ? (
                <span className="badge good"><span className="dot pass" />Active</span>
              ) : daemonOnline === false ? (
                <span className="badge"><span className="dot warn" />CLI only</span>
              ) : (
                <span className="badge"><span className="dot" />Checking…</span>
              )}
              <span className="row-chevron"><Icon name="chevron" /></span>
            </div>
            <div className="row">
              <div className="row-icon"><Icon name="folder" /></div>
              <div className="row-main">
                <div className="row-title">Device details</div>
                <div className="row-sub">Configuration, paths, system info &amp; live metrics</div>
              </div>
              <span className="row-chevron"><Icon name="chevron" /></span>
            </div>
          </Link>
        </div>
      )}

      <div className="group">
        <div className="group-title">Trusted devices</div>
        <div className="group-body">
          {peers.length === 0 ? (
            <div className="row">
              <div className="row-icon"><Icon name="share" /></div>
              <div className="row-main">
                <div className="row-title">No devices yet</div>
                <div className="row-sub">Pair a peer with <code>hyprshare peer pair</code> to see it here.</div>
              </div>
            </div>
          ) : (
            peers.map(peer => (
              <div key={peer.device_id} className="row">
                <div className="row-icon"><Icon name="device" /></div>
                <div className="row-main">
                  <div className="row-title">{peer.device_label || peer.device_id}</div>
                  <div className="row-sub mono">{peer.device_id}</div>
                </div>
              </div>
            ))
          )}
        </div>
      </div>

      {health && (
        <div className="group">
          <div className="group-title">System health</div>
          <div className="group-body">
            {health.results.filter(result => result.status !== 'pass').length === 0 ? (
              <div className="row">
                <div className="row-icon"><Icon name="check" /></div>
                <div className="row-main">
                  <div className="row-title">Everything looks good</div>
                  <div className="row-sub">All {health.checks} checks are passing.</div>
                </div>
              </div>
            ) : (
              health.results
                .filter(result => result.status !== 'pass')
                .map(result => (
                  <div key={result.id} className="row">
                    <div className="row-icon"><Icon name="alert" /></div>
                    <div className="row-main">
                      <div className="row-title">{result.id}</div>
                      <div className="row-sub">{result.summary}</div>
                    </div>
                    <span className={`badge ${result.status === 'warn' ? 'warn' : 'bad'}`}>
                      <span className={`dot ${result.status}`} />
                      {result.status}
                    </span>
                  </div>
                ))
            )}
          </div>
        </div>
      )}
    </>
  )
}

export default Dashboard
