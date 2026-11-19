import { useState } from 'react'
import { runJson, runAction, desktopAvailable } from '../api'
import { PageHeader, Group, Row, StatusBanner, EmptyState, ActionButton } from '../components/ui'

interface Snapshot {
  cpu: number
  memory: number
  disk: number
  network_sent: number
  network_recv: number
  connections: number
}
interface NetInterface { name: string; address: string; up: boolean }
interface SearchResult { name: string; path: string; is_dir: boolean; size: number }
interface Backup { backup_id: string; created_at?: number; description?: string }

function Tools() {
  const [message, setMessage] = useState<string | null>(null)
  const [error, setError] = useState<string | null>(null)
  const [snapshot, setSnapshot] = useState<Snapshot | null>(null)
  const [interfaces, setInterfaces] = useState<NetInterface[]>([])
  const [searchPath, setSearchPath] = useState('.')
  const [searchPattern, setSearchPattern] = useState('*')
  const [results, setResults] = useState<SearchResult[] | null>(null)
  const [tlsStatus, setTlsStatus] = useState<{ has_cert: boolean } | null>(null)
  const [backups, setBackups] = useState<Backup[]>([])
  const [latencyHost, setLatencyHost] = useState('')

  const act = async (args: string[], success: string) => {
    setError(null)
    setMessage(null)
    try {
      const result = await runAction(args)
      setMessage(success + (result.trim() ? ` — ${result.trim().slice(0, 200)}` : ''))
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
    }
  }

  const doSearch = async () => {
    setError(null)
    setResults(null)
    try {
      const data = await runJson<SearchResult[]>([
        'search', 'files', '--path', searchPath, '--pattern', searchPattern
      ])
      setResults(data)
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
    }
  }

  const refreshSnapshot = async () => {
    setError(null)
    try {
      setSnapshot(await runJson<Snapshot>(['monitor', 'snapshot', '--json']))
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
    }
  }

  const refreshNet = async () => {
    setError(null)
    try {
      setInterfaces(await runJson<NetInterface[]>(['netdiag', 'interfaces', '--json']))
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
    }
  }

  const refreshTls = async () => {
    setError(null)
    try {
      setTlsStatus(await runJson<{ has_cert: boolean }>(['tls', 'status', '--json']))
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
    }
  }

  const refreshBackups = async () => {
    setError(null)
    try {
      setBackups(await runJson<Backup[]>(['backup', 'list', '--json']))
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
    }
  }

  return (
    <>
      <PageHeader
        eyebrow="Utilities"
        title="Tools"
        subtitle="Search files, inspect the network, check system metrics, and manage certificates and backups."
      />

      {!desktopAvailable() && (
        <StatusBanner kind="message">These tools run CLI commands and require the desktop app.</StatusBanner>
      )}
      {error && <StatusBanner kind="error">{error}</StatusBanner>}
      {message && <StatusBanner kind="message">{message}</StatusBanner>}

      <Group title="System snapshot">
        {snapshot ? (
          <div className="stat-grid" style={{ marginBottom: 0 }}>
            <div className="stat"><div className="stat-num">{snapshot.cpu.toFixed(1)}%</div><div className="stat-label">CPU</div></div>
            <div className="stat"><div className="stat-num">{snapshot.memory.toFixed(1)}%</div><div className="stat-label">Memory</div></div>
            <div className="stat"><div className="stat-num">{snapshot.disk.toFixed(1)}%</div><div className="stat-label">Disk</div></div>
            <div className="stat"><div className="stat-num">{snapshot.connections}</div><div className="stat-label">Connections</div></div>
          </div>
        ) : (
          <EmptyState icon="tools" title="No snapshot yet" sub="Collect a live snapshot of this machine." />
        )}
        <div className="row" style={{ justifyContent: 'flex-end' }}>
          <ActionButton variant="secondary" icon="refresh" onClick={refreshSnapshot}>Refresh snapshot</ActionButton>
        </div>
      </Group>

      <Group title="File search">
        <div className="row" style={{ flexDirection: 'column', alignItems: 'stretch', gap: 10 }}>
          <input className="text-input" placeholder="Directory to search (e.g. ~/Projects)" value={searchPath} onChange={e => setSearchPath(e.target.value)} />
          <input className="text-input" placeholder="Pattern (e.g. *.cpp)" value={searchPattern} onChange={e => setSearchPattern(e.target.value)} />
          <div style={{ display: 'flex', justifyContent: 'flex-end' }}>
            <ActionButton icon="search" onClick={doSearch}>Search</ActionButton>
          </div>
        </div>
        {results !== null && (
          <div className="hint-text mono" style={{ maxHeight: 240, overflow: 'auto' }}>
            {results.length === 0 ? 'No matches.' : results.slice(0, 20).map(r => `${r.is_dir ? '[dir] ' : ''}${r.name} — ${r.path}`).join('\n')}
          </div>
        )}
      </Group>

      <Group title="Network">
        {interfaces.length > 0 && (
          <div className="hint-text mono" style={{ maxHeight: 160, overflow: 'auto' }}>
            {interfaces.map(i => `${i.name.trim()} ${i.up ? 'up' : 'down'} ${i.address}`).join('\n')}
          </div>
        )}
        <div className="row" style={{ flexDirection: 'column', alignItems: 'stretch', gap: 10 }}>
          <input className="text-input" placeholder="Host for a latency test" value={latencyHost} onChange={e => setLatencyHost(e.target.value)} />
          <div style={{ display: 'flex', gap: 8, justifyContent: 'flex-end', flexWrap: 'wrap' }}>
            <ActionButton variant="secondary" icon="network" onClick={refreshNet}>List interfaces</ActionButton>
            <ActionButton
              variant="secondary"
              icon="refresh"
              disabled={!latencyHost.trim()}
              onClick={() => act(['netdiag', 'latency', latencyHost.trim()], 'Latency test')}
            >
              Test latency
            </ActionButton>
          </div>
        </div>
      </Group>

      <Group title="TLS certificate">
        <Row
          icon="shield"
          title={tlsStatus ? (tlsStatus.has_cert ? 'Certificate present' : 'No certificate') : 'Status unknown'}
          sub="Used for authenticated peer connections."
          right={<ActionButton variant="secondary" icon="refresh" onClick={refreshTls}>Check</ActionButton>}
        />
        <div className="row" style={{ justifyContent: 'flex-end' }}>
          <ActionButton variant="secondary" icon="plus" onClick={() => act(['tls', 'generate'], 'Certificate generated')}>
            Generate certificate
          </ActionButton>
        </div>
      </Group>

      <Group title={`Backups · ${backups.length}`}>
        {backups.length === 0 ? (
          <EmptyState icon="archive" title="No backups yet" sub="Create a backup of your hyprshare state." />
        ) : (
          backups.slice(0, 10).map((b, i) => (
            <Row key={i} icon="archive" title={b.description || b.backup_id} sub={b.backup_id} />
          ))
        )}
        <div className="row" style={{ justifyContent: 'flex-end', gap: 8 }}>
          <ActionButton variant="secondary" icon="refresh" onClick={refreshBackups}>Refresh</ActionButton>
          <ActionButton icon="plus" onClick={() => act(['backup', 'create'], 'Backup created')}>Create backup</ActionButton>
        </div>
      </Group>
    </>
  )
}

export default Tools
