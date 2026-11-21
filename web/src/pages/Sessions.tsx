import { useState, useEffect } from 'react'
import {
  runJson, runAction, serveSession, desktopAvailable,
  type ServeSessionInfo,
} from '../api'
import { PageHeader, Group, Row, StatusBanner, EmptyState, ActionButton } from '../components/ui'

interface Session {
  session_id: string
  capability: string
  share_name: string
  file_count: number
  total_bytes: number
}

interface SessionDetail {
  session_id: string
  stage_path: string
  copied_file_count: number
  copied_bytes: number
  manifest: {
    capability: string
    share_name: string
    file_count?: number
    total_bytes?: number
  }
}

interface VerifyResult {
  verified: boolean
}

function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`
}

function Sessions() {
  const [sessions, setSessions] = useState<Session[]>([])
  const [error, setError] = useState<string | null>(null)
  const [message, setMessage] = useState<string | null>(null)
  const [serving, setServing] = useState<ServeSessionInfo | null>(null)
  const [expanded, setExpanded] = useState<string | null>(null)
  const [detail, setDetail] = useState<SessionDetail | null>(null)
  const [verified, setVerified] = useState<boolean | null>(null)

  const load = async () => {
    try {
      setSessions(await runJson<Session[]>(['session', 'list', '--json']))
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

  const startServing = async (sessionId: string) => {
    setError(null)
    setMessage(null)
    try {
      // Random high port avoids colliding with an existing listener.
      const port = 30000 + Math.floor(Math.random() * 20000)
      const info = await serveSession(sessionId, port)
      setServing(info)
      setExpanded(null)
      setMessage(`Serving ${sessionId} on ${info.address}.`)
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
    }
  }

  const toggleDetails = async (sessionId: string) => {
    if (expanded === sessionId) {
      setExpanded(null)
      return
    }
    setError(null)
    setExpanded(sessionId)
    setDetail(null)
    setVerified(null)
    try {
      const [show, verify] = await Promise.all([
        runJson<SessionDetail>(['session', 'show', sessionId, '--json']),
        runJson<VerifyResult>(['session', 'verify', sessionId, '--json']),
      ])
      setDetail(show)
      setVerified(verify.verified)
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
    }
  }

  return (
    <>
      <PageHeader
        eyebrow="Sessions"
        title="Share Sessions"
        subtitle="Staged shares waiting to be served. Start a session, hand the receiver a token, and transfer."
      />

      {!desktopAvailable() && (
        <StatusBanner kind="message">Session actions require the desktop app.</StatusBanner>
      )}
      {error && <StatusBanner kind="error">{error}</StatusBanner>}
      {message && <StatusBanner kind="message">{message}</StatusBanner>}

      <Group
        title={`Active sessions · ${sessions.length}`}
        hint="Create one from File Transfer or by sharing a file/folder/clipboard."
      >
        {sessions.length === 0 ? (
          <EmptyState
            icon="share"
            title="No staged sessions"
            sub="Share a file, folder, or clipboard entry to stage a session here."
          />
        ) : (
          <>
            {sessions.map(session => (
              <div key={session.session_id}>
                <Row
                  icon="share"
                  title={session.share_name}
                  sub={`${session.capability} · ${session.file_count} files · ${formatBytes(session.total_bytes)}`}
                  value={<span className="mono">{session.session_id}</span>}
                  right={
                    <span style={{ display: 'inline-flex', gap: 8, flexWrap: 'wrap' }}>
                      <ActionButton
                        variant="secondary"
                        icon="play"
                        onClick={() => startServing(session.session_id)}
                      >
                        Serve
                      </ActionButton>
                      <ActionButton
                        variant="secondary"
                        icon="file"
                        onClick={() => toggleDetails(session.session_id)}
                      >
                        {expanded === session.session_id ? 'Hide' : 'Details'}
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
                {expanded === session.session_id && (
                  <div className="row" style={{ flexDirection: 'column', alignItems: 'stretch', gap: 8 }}>
                    {detail ? (
                      <>
                        <div className="row-sub">
                          {detail.copied_file_count} files · {formatBytes(detail.copied_bytes)} staged at{" "}
                          <span className="mono">{detail.stage_path}</span>
                        </div>
                        <div className="row-sub">
                          Payload verification:{" "}
                          <span className={`badge ${verified ? 'good' : 'warn'}`}>
                            <span className={`dot ${verified ? 'pass' : 'warn'}`} />
                            {verified === null ? 'checking…' : verified ? 'OK' : 'FAILED'}
                          </span>
                        </div>
                      </>
                    ) : (
                      <div className="row-sub">Loading details…</div>
                    )}
                  </div>
                )}
              </div>
            ))}
            <div className="row" style={{ justifyContent: 'flex-end' }}>
              <ActionButton
                variant="secondary"
                icon="trash"
                onClick={() => act(['session', 'prune', '--all'], 'All sessions pruned')}
              >
                Clear all
              </ActionButton>
            </div>
          </>
        )}
      </Group>

      {serving && (
        <Group title="Active transfer">
          <div className="row" style={{ flexDirection: 'column', alignItems: 'stretch', gap: 8 }}>
            <div className="row-title">Serving on {serving.address}</div>
            <div className="row-sub">Give the receiver this command:</div>
            <code style={{ userSelect: 'all', padding: 10 }}>
              hyprshare session fetch {serving.address} --token {serving.token} --to ~/Downloads
            </code>
            <div className="row-sub">Token: <span className="mono">{serving.token}</span></div>
          </div>
        </Group>
      )}
    </>
  )
}

export default Sessions
