import { useState, useEffect } from 'react'
import { runJson, runAction, pickPath, serveSession, type ServeSessionInfo, desktopAvailable } from '../api'
import { PageHeader, Group, Row, StatusBanner, EmptyState, ActionButton } from '../components/ui'

interface SelfIdentity {
  device_id: string
  device_label: string
  created_at_unix: number
}
interface Peer {
  device_id: string
  device_label?: string
  label?: string
}
interface Session {
  session_id: string
  capability: string
  share_name: string
  file_count: number
  total_bytes: number
}
interface Discovery {
  avahi_daemon_active: boolean
  ready_to_announce: boolean
  ready_to_browse: boolean
}

function Devices() {
  const [self, setSelf] = useState<SelfIdentity | null>(null)
  const [peers, setPeers] = useState<Peer[]>([])
  const [sessions, setSessions] = useState<Session[]>([])
  const [discovery, setDiscovery] = useState<Discovery | null>(null)
  const [message, setMessage] = useState<string | null>(null)
  const [error, setError] = useState<string | null>(null)
  const [serving, setServing] = useState<ServeSessionInfo | null>(null)

  const startServing = async (sessionId: string) => {
    setError(null)
    setMessage(null)
    try {
      // 29112 is the CLI default receiver port; choose a high ephemeral port
      // per click to avoid colliding with an existing listener.
      const port = 30000 + Math.floor(Math.random() * 20000)
      const info = await serveSession(sessionId, port)
      setServing(info)
      setMessage(`Serving ${sessionId} on ${info.address}.`)
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
    }
  }
  const [invitePath, setInvitePath] = useState('')
  const [pairPath, setPairPath] = useState('')

  const load = async () => {
    try {
      const [selfData, peerData, sessionData, discData] = await Promise.all([
        runJson<SelfIdentity>(['peer', 'self', '--json']),
        runJson<Peer[]>(['peer', 'list', '--json']),
        runJson<Session[]>(['session', 'list', '--json']),
        runJson<Discovery>(['discovery', 'status', '--json'])
      ])
      setSelf(selfData)
      setPeers(peerData)
      setSessions(sessionData)
      setDiscovery(discData)
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
    }
  }

  useEffect(() => {
    load()
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  const act = async (args: string[], success: string) => {
    setError(null)
    setMessage(null)
    try {
      const result = await runAction(args)
      setMessage(success + (result.trim() ? ` — ${result.trim()}` : ''))
      await load()
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
    }
  }

  return (
    <>
      <PageHeader
        eyebrow="Devices"
        title="Devices & Sessions"
        subtitle="Your identity, trusted peers, active share sessions, and local discovery."
      />

      {!desktopAvailable() && (
        <StatusBanner kind="message">Device actions require the desktop app.</StatusBanner>
      )}
      {error && <StatusBanner kind="error">{error}</StatusBanner>}
      {message && <StatusBanner kind="message">{message}</StatusBanner>}

      {self && (
        <Group title="This device">
          <Row icon="device" title={self.device_label} sub="Your device ID" value={<span className="mono">{self.device_id}</span>} />
        </Group>
      )}

      <Group title={`Trusted peers · ${peers.length}`}>
        {peers.length === 0 ? (
          <EmptyState icon="devices" title="No trusted peers" sub="Pair with another device using an invitation file." />
        ) : (
          peers.map(peer => (
            <Row
              key={peer.device_id}
              icon="device"
              title={peer.device_label || peer.label || peer.device_id}
              sub="Trusted peer"
              value={<span className="mono">{peer.device_id.slice(0, 12)}…</span>}
            />
          ))
        )}
      </Group>

      <Group title="Pairing">
        <div className="row" style={{ flexDirection: 'column', alignItems: 'stretch', gap: 10 }}>
          <input
            className="text-input"
            placeholder="Invitation file path to create"
            value={invitePath}
            onChange={e => setInvitePath(e.target.value)}
          />
          <div style={{ display: 'flex', gap: 8, justifyContent: 'flex-end' }}>
            <ActionButton
              variant="secondary"
              icon="plus"
              disabled={!invitePath.trim()}
              onClick={() => act(['peer', 'invite', 'create', '--to', invitePath.trim()], 'Invitation created')}
            >
              Create invitation
            </ActionButton>
          </div>
          <input
            className="text-input"
            placeholder="Invitation file path to pair with"
            value={pairPath}
            onChange={e => setPairPath(e.target.value)}
          />
          <div style={{ display: 'flex', gap: 8, justifyContent: 'flex-end', flexWrap: 'wrap' }}>
            <ActionButton
              variant="secondary"
              icon="file"
              onClick={async () => {
                const picked = await pickPath('file')
                if (picked) setPairPath(picked)
              }}
            >
              Choose invitation…
            </ActionButton>
            <ActionButton
              icon="link"
              disabled={!pairPath.trim()}
              onClick={() => act(['peer', 'pair', pairPath.trim()], 'Paired with peer')}
            >
              Pair with peer
            </ActionButton>
          </div>
        </div>
      </Group>

      <Group title={`Share sessions · ${sessions.length}`}>
        {sessions.length === 0 ? (
          <EmptyState icon="share" title="No active sessions" sub="Sessions you create for sharing will appear here." />
        ) : (
          sessions.slice(0, 12).map(session => (
            <Row
              key={session.session_id}
              icon="share"
              title={session.share_name}
              sub={`${session.capability} · ${session.file_count} files · ${(session.total_bytes / 1024).toFixed(1)} KB`}
              value={<span className="mono">{session.session_id}</span>}
              right={
                <span style={{ display: 'inline-flex', gap: 8 }}>
                  <ActionButton
                    variant="secondary"
                    icon="play"
                    onClick={() => startServing(session.session_id)}
                  >
                    Serve
                  </ActionButton>
                  <ActionButton
                    variant="secondary"
                    icon="trash"
                    onClick={() => act(['session', 'delete', session.session_id], 'Session deleted')}
                  >
                    Delete
                  </ActionButton>
                </span>
              }
            />
          ))
        )}
        {serving && (
          <div className="row" style={{ flexDirection: 'column', alignItems: 'stretch', gap: 8 }}>
            <div className="row-title">Serving on {serving.address}</div>
            <div className="row-sub">Give the receiver this command:</div>
            <code style={{ userSelect: 'all', padding: 10 }}>
              hyprshare session fetch {serving.address} --token {serving.token} --to ~/Downloads
            </code>
            <div className="row-sub">Token: <span className="mono">{serving.token}</span></div>
          </div>
        )}
      </Group>

      <Group title="Local discovery">
        {discovery && (
          <Row
            icon="network"
            title={discovery.avahi_daemon_active ? 'mDNS discovery active' : 'mDNS discovery inactive'}
            sub={`Announce: ${discovery.ready_to_announce ? 'ready' : 'unavailable'} · Browse: ${discovery.ready_to_browse ? 'ready' : 'unavailable'}`}
            right={
              <span className={`badge ${discovery.avahi_daemon_active ? 'good' : 'warn'}`}>
                <span className={`dot ${discovery.avahi_daemon_active ? 'pass' : 'warn'}`} />
                {discovery.avahi_daemon_active ? 'Ready' : 'Start avahi-daemon'}
              </span>
            }
          />
        )}
        <div className="row" style={{ justifyContent: 'flex-end' }}>
          <ActionButton variant="secondary" icon="refresh" onClick={() => act(['discovery', 'browse', '--json'], 'Discovery refreshed')}>
            Browse network
          </ActionButton>
        </div>
      </Group>
    </>
  )
}

export default Devices
