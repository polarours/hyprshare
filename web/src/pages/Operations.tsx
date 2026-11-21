import { useState, useEffect } from 'react'
import { runJson, runAction, pickPath, desktopAvailable } from '../api'
import { PageHeader, Group, Row, StatusBanner, EmptyState, ActionButton } from '../components/ui'

interface Backup { backup_id: string; description?: string; created_at?: number }
interface Plugin { name: string; path?: string; enabled?: boolean }
interface Profile { id: string; name: string; active?: boolean }
interface AuditCheck { id: string; name: string; status: string; remediation?: string }
interface AuditReport { checks: AuditCheck[]; score: number; passed: number; failed: number; warnings: number }
interface RecordStatus { recording: boolean; format?: string; output?: string }
interface UpdateStatus { version?: string; status?: string }

function Operations() {
  const [error, setError] = useState<string | null>(null)
  const [message, setMessage] = useState<string | null>(null)
  const [backups, setBackups] = useState<Backup[]>([])
  const [plugins, setPlugins] = useState<Plugin[]>([])
  const [profiles, setProfiles] = useState<Profile[]>([])
  const [audit, setAudit] = useState<AuditReport | null>(null)
  const [record, setRecord] = useState<RecordStatus | null>(null)
  const [update, setUpdate] = useState<UpdateStatus | null>(null)
  const [auditDetail, setAuditDetail] = useState('')
  const [backupLabel, setBackupLabel] = useState('')

  const refresh = async () => {
    setError(null)
    const read = async <T,>(args: string[]): Promise<T | null> => {
      try { return await runJson<T>(args, 0) } catch { return null }
    }
    const [b, p, pr, r, u] = await Promise.all([
      read<Backup[]>(['backup', 'list', '--json']),
      read<Plugin[]>(['plugin', 'list', '--json']),
      read<Profile[]>(['profile', 'list', '--json']),
      read<RecordStatus>(['record', 'status', '--json']),
      read<UpdateStatus>(['update', 'status', '--json']),
    ])
    setBackups(Array.isArray(b) ? b : [])
    setPlugins(Array.isArray(p) ? p : [])
    setProfiles(Array.isArray(pr) ? pr : [])
    setRecord(r)
    setUpdate(u)
  }

  useEffect(() => { refresh() /* eslint-disable-line react-hooks/exhaustive-deps */ }, [])

  const act = async (args: string[], success: string) => {
    setError(null)
    setMessage(null)
    try {
      const result = await runAction(args)
      setMessage(success + (result.trim() ? ` — ${result.trim().slice(0, 160)}` : ''))
      await refresh()
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
    }
  }

  const runAudit = async () => {
    setError(null)
    try {
      setAudit(await runJson<AuditReport>(['audit', 'run', '--json'], 0))
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
    }
  }

  return (
    <>
      <PageHeader
        eyebrow="Operations"
        title="Operations"
        subtitle="Security audits, backups, profiles, plugins, recording and updates — all through the CLI."
      />

      {!desktopAvailable() && (
        <StatusBanner kind="message">Operations require the desktop app.</StatusBanner>
      )}
      {error && <StatusBanner kind="error">{error}</StatusBanner>}
      {message && <StatusBanner kind="message">{message}</StatusBanner>}

      <Group title="Security audit" hint="Real permission, network and service checks.">
        <Row
          icon="check"
          title="Run a security audit"
          sub={audit
            ? `${audit.passed} passed · ${audit.failed} failed · ${audit.warnings} warnings · score ${audit.score}%`
            : 'Inspect TLS, file permissions, inbound tokens and the daemon service.'}
          right={<ActionButton variant="secondary" icon="refresh" onClick={runAudit}>Run audit</ActionButton>}
        />
        {audit && (
          <div className="row" style={{ flexDirection: 'column', alignItems: 'stretch', gap: 6 }}>
            {audit.checks.map(check => (
              <div key={check.id} className="row-sub">
                <span className={`dot ${check.status === 'pass' ? 'pass' : check.status === 'fail' ? 'fail' : 'warn'}`} />
                {' '}{check.name} — {check.status}
                {check.remediation ? ` · ${check.remediation}` : ''}
              </div>
            ))}
          </div>
        )}
        <div className="row" style={{ flexDirection: 'column', alignItems: 'stretch', gap: 8 }}>
          <input className="text-input" placeholder="Check id (e.g. file_permissions)" value={auditDetail} onChange={e => setAuditDetail(e.target.value)} />
          <div style={{ display: 'flex', justifyContent: 'flex-end' }}>
            <ActionButton variant="secondary" disabled={!auditDetail.trim()} onClick={() => act(['audit', 'check', auditDetail.trim()], 'Audit check run')}>Check one</ActionButton>
          </div>
        </div>
      </Group>

      <Group title={`Backups · ${backups.length}`}>
        {backups.length === 0 ? (
          <EmptyState icon="folder" title="No backups yet" sub="Create a backup of your hyprshare config and state." />
        ) : (
          backups.map(b => (
            <Row
              key={b.backup_id}
              icon="folder"
              title={b.description || b.backup_id}
              sub={`${b.backup_id}${b.created_at ? ` · ${new Date(b.created_at * 1000).toLocaleString()}` : ''}`}
              right={<ActionButton variant="secondary" icon="trash" onClick={() => act(['backup', 'delete', b.backup_id], 'Backup deleted')}>Delete</ActionButton>}
            />
          ))
        )}
        <div className="row" style={{ flexDirection: 'column', alignItems: 'stretch', gap: 8 }}>
          <input className="text-input" placeholder="Backup description (optional)" value={backupLabel} onChange={e => setBackupLabel(e.target.value)} />
          <div style={{ display: 'flex', justifyContent: 'flex-end' }}>
            <ActionButton icon="plus" onClick={() => act(backupLabel.trim() ? ['backup', 'create', '--description', backupLabel.trim()] : ['backup', 'create'], 'Backup created')}>Create backup</ActionButton>
          </div>
        </div>
      </Group>

      <Group title={`Profiles · ${profiles.length}`}>
        {profiles.length === 0 ? (
          <EmptyState icon="device" title="No profiles" sub="Profiles let you switch configuration sets." />
        ) : (
          profiles.map(p => (
            <Row
              key={p.id}
              icon="device"
              title={p.name}
              sub={p.id}
              value={p.active ? <span className="badge good">Active</span> : undefined}
              right={<ActionButton variant="secondary" onClick={() => act(['profile', 'switch', p.id], 'Profile switched')}>Switch</ActionButton>}
            />
          ))
        )}
      </Group>

      <Group title={`Plugins · ${plugins.length}`}>
        {plugins.length === 0 ? (
          <EmptyState icon="tools" title="No plugins loaded" sub="Load a shared object to add CLI commands." />
        ) : (
          plugins.map(p => (
            <Row
              key={p.name}
              icon="tools"
              title={p.name}
              sub={p.path || 'shared object'}
              value={p.enabled ? <span className="badge good">Enabled</span> : <span className="badge">Disabled</span>}
              right={<ActionButton variant="secondary" onClick={() => act(['plugin', p.enabled ? 'disable' : 'enable', p.name], 'Plugin updated')}>{p.enabled ? 'Disable' : 'Enable'}</ActionButton>}
            />
          ))
        )}
        <div className="row" style={{ justifyContent: 'flex-end' }}>
          <ActionButton variant="secondary" icon="plus" onClick={async () => {
            const path = await pickPath('file')
            if (path) await act(['plugin', 'load', path], 'Plugin loaded')
          }}>Load plugin…</ActionButton>
        </div>
      </Group>

      <Group title="Recording & updates">
        <Row
          icon="record"
          title="Screen recording"
          sub={record?.recording ? `Recording ${record.format || ''} to ${record.output || '…'}` : 'Idle'}
          right={
            <span style={{ display: 'inline-flex', gap: 8 }}>
              <ActionButton variant="secondary" onClick={() => act(['record', 'start'], 'Recording started')}>Start</ActionButton>
              <ActionButton variant="secondary" onClick={() => act(['record', 'stop'], 'Recording stopped')}>Stop</ActionButton>
            </span>
          }
        />
        <Row
          icon="refresh"
          title="Software update"
          sub={update?.version ? `Installed ${update.version}` : 'Check the release manifest for a newer version.'}
          right={<ActionButton variant="secondary" icon="refresh" onClick={() => act(['update', 'check'], 'Update check complete')}>Check</ActionButton>}
        />
      </Group>
    </>
  )
}

export default Operations
