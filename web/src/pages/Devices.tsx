import { useState, useEffect } from 'react'
import { runJson, runAction, pickPath, savePath, desktopAvailable } from '../api'
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
interface Discovery {
  avahi_daemon_active: boolean
  ready_to_announce: boolean
  ready_to_browse: boolean
}
interface InviteSummary {
  invite_kind: string
  from_device_id: string
  from_device_label: string
  created_at_unix: number
  invite_path?: string
}

function Devices() {
  const [self, setSelf] = useState<SelfIdentity | null>(null)
  const [peers, setPeers] = useState<Peer[]>([])
  const [discovery, setDiscovery] = useState<Discovery | null>(null)
  const [browseResult, setBrowseResult] = useState<string | null>(null)
  const [message, setMessage] = useState<string | null>(null)
  const [error, setError] = useState<string | null>(null)
  const [inspected, setInspected] = useState<InviteSummary | null>(null)
  const [pendingRemove, setPendingRemove] = useState<string | null>(null)

  const browseNetwork = async () => {
    setError(null)
    setBrowseResult(null)
    try {
      const result = await runJson<unknown>(['discovery', 'browse', '--json'], 0)
      setBrowseResult(JSON.stringify(result, null, 2))
    } catch (cause) {
      setBrowseResult(cause instanceof Error ? cause.message : String(cause))
    }
  }

  const load = async () => {
    try {
      const [selfData, peerData, discData] = await Promise.all([
        runJson<SelfIdentity>(['peer', 'self', '--json']),
        runJson<Peer[]>(['peer', 'list', '--json']),
        runJson<Discovery>(['discovery', 'status', '--json'])
      ])
      setSelf(selfData)
      setPeers(peerData)
      setDiscovery(discData)
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
    }
  }

  useEffect(() => {
    load()
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  /** Runs a CLI action and shows what it returned, then reloads the page. */
  const act = async (args: string[], success: string) => {
    setError(null)
    setMessage(null)
    try {
      const result = await runAction(args)
      const parsed = result.trim() ? safeParse(result) : null
      const detail = parsed && typeof parsed === 'object' && 'invite_path' in parsed
        ? ` → ${String((parsed as InviteSummary).invite_path)}`
        : result.trim() && !result.trim().startsWith('{')
          ? ` — ${result.trim()}`
          : ''
      setMessage(success + detail)
      await load()
      return parsed
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
      return null
    }
  }

  const createInvite = async () => {
    const target = await savePath('hyprshare-invite.invite', 'Where to save your invitation')
    if (!target) return
    await act(['peer', 'invite', 'create', '--to', target, '--json'], 'Invitation created')
  }

  const inspectInvite = async () => {
    const source = await pickPath('file')
    if (!source) return
    setError(null)
    setMessage(null)
    setInspected(null)
    try {
      const summary = await runJson<InviteSummary>(
        ['peer', 'invite', 'inspect', source, '--json'],
        0,
      )
      setInspected(summary)
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
    }
  }

  const respondToInvite = async () => {
    const source = await pickPath('file')
    if (!source) return
    const target = await savePath('hyprshare-response.invite', 'Where to save your response')
    if (!target) return
    await act(
      ['peer', 'invite', 'respond', source, '--to', target, '--json'],
      'Response created — send it back to the other device',
    )
  }

  const completePairing = async () => {
    const source = await pickPath('file')
    if (!source) return
    await act(['peer', 'invite', 'complete', source, '--json'], 'Pairing complete')
  }

  const trustDeviceFile = async () => {
    const source = await pickPath('file')
    if (!source) return
    await act(['peer', 'pair', source, '--json'], 'Device trusted')
  }

  const exportIdentity = async () => {
    const target = await savePath(
      `${self?.device_id ?? 'device'}.identity.json`,
      'Where to save your device identity',
    )
    if (!target) return
    await act(['peer', 'export', '--to', target, '--json'], 'Identity exported')
  }

  return (
    <>
      <PageHeader
        eyebrow="Devices"
        title="Devices"
        subtitle="Your identity, trusted peers, and local discovery."
      />

      {!desktopAvailable() && (
        <StatusBanner kind="message">Device actions require the desktop app.</StatusBanner>
      )}
      {error && <StatusBanner kind="error">{error}</StatusBanner>}
      {message && <StatusBanner kind="message">{message}</StatusBanner>}

      {self && (
        <Group title="This device">
          <Row
            icon="device"
            title={self.device_label}
            sub="Your device ID"
            value={<span className="mono">{self.device_id}</span>}
            right={
              <ActionButton variant="secondary" icon="upload" onClick={exportIdentity}>
                Export identity
              </ActionButton>
            }
          />
        </Group>
      )}

      <Group title={`Trusted peers · ${peers.length}`}>
        {peers.length === 0 ? (
          <EmptyState
            icon="devices"
            title="No trusted peers"
            sub="Pair using an invitation file, or trust an exported identity file."
          />
        ) : (
          peers.map(peer => (
            <Row
              key={peer.device_id}
              icon="device"
              title={peer.device_label || peer.label || peer.device_id}
              sub="Trusted peer"
              value={<span className="mono">{peer.device_id.slice(0, 12)}…</span>}
              right={
                pendingRemove === peer.device_id ? (
                  <div className="row-inline-inputs">
                    <ActionButton
                      variant="secondary"
                      onClick={() => setPendingRemove(null)}
                    >
                      Cancel
                    </ActionButton>
                    <ActionButton
                      icon="trash"
                      onClick={async () => {
                        setPendingRemove(null)
                        await act(['peer', 'remove', peer.device_id, '--json'], 'Stopped trusting')
                      }}
                    >
                      Confirm
                    </ActionButton>
                  </div>
                ) : (
                  <ActionButton
                    variant="secondary"
                    icon="trash"
                    onClick={() => setPendingRemove(peer.device_id)}
                  >
                    Untrust
                  </ActionButton>
                )
              }
            />
          ))
        )}
      </Group>

      <Group
        title="Pair a device"
        hint="Pairing exchanges two small files. Each side sends one back to the other."
      >
        <Row
          icon="plus"
          title="1 · Invite another device"
          sub="Writes an invitation file — send it to the other device."
          right={
            <ActionButton icon="plus" onClick={createInvite}>
              Create invitation
            </ActionButton>
          }
        />
        <Row
          icon="file"
          title="2 · Accept an invitation"
          sub="Opens the file they sent, then writes a response for you to send back."
          right={
            <div className="row-inline-inputs">
              <ActionButton variant="secondary" icon="search" onClick={inspectInvite}>
                Inspect…
              </ActionButton>
              <ActionButton icon="link" onClick={respondToInvite}>
                Accept…
              </ActionButton>
            </div>
          }
        />
        {inspected && (
          <div className="hint-text">
            {inspected.invite_kind === 'request' ? (
              <>
                Invitation from <strong>{inspected.from_device_label}</strong>{' '}
                (<span className="mono">{inspected.from_device_id.slice(0, 12)}…</span>). Accepting
                trusts this device on your side and writes a response for them.
              </>
            ) : (
              <>
                This is a <strong>{inspected.invite_kind}</strong> reply from{' '}
                <strong>{inspected.from_device_label}</strong> — finish the pairing with it instead
                of accepting it.
              </>
            )}
          </div>
        )}
        <Row
          icon="check"
          title="3 · Finish pairing"
          sub="Opens the response they sent back and adds them as a trusted peer."
          right={
            <ActionButton icon="check" onClick={completePairing}>
              Finish…
            </ActionButton>
          }
        />
        <Row
          icon="shield"
          title="Trust an exported identity"
          sub="One-way trust: add a device from the identity file it exported."
          right={
            <ActionButton variant="secondary" icon="upload" onClick={trustDeviceFile}>
              Choose identity…
            </ActionButton>
          }
        />
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
          <ActionButton variant="secondary" icon="refresh" onClick={browseNetwork}>
            Browse network
          </ActionButton>
        </div>
        {browseResult && (
          <div className="hint-text mono" style={{ whiteSpace: 'pre-wrap', maxHeight: 180, overflow: 'auto' }}>
            {browseResult}
          </div>
        )}
      </Group>
    </>
  )
}

function safeParse(text: string): unknown {
  try {
    return JSON.parse(text)
  } catch {
    return null
  }
}

export default Devices
