import React, { useState, useEffect } from 'react'
import { api, uploadFiles, runJson, runAction, pickPath, desktopAvailable, type Inbox } from '../api'
import { Icon } from '../components/Icon'
import { PageHeader, Group, Row, StatusBanner, EmptyState, ActionButton } from '../components/ui'

interface InboxStatus {
  configured_receive_dir: string
  resolved_receive_dir: string
  exists: boolean
  is_directory: boolean
  writable: boolean
  default_host: string
  default_port: number
  advertised_hosts: string[]
}

interface HistoryEntry {
  entry_id: string
  file_name: string
  bytes_received: number
  received_at_unix: number
  sha256?: string
}

function FileTransfer() {
  const [files, setFiles] = useState<File[]>([])
  const [inbox, setInbox] = useState<Inbox | null>(null)
  const [status, setStatus] = useState<InboxStatus | null>(null)
  const [history, setHistory] = useState<HistoryEntry[]>([])
  const [uploading, setUploading] = useState(false)
  const [message, setMessage] = useState<string | null>(null)
  const [error, setError] = useState<string | null>(null)
  const [shareResult, setShareResult] = useState<string | null>(null)

  const load = async () => {
    try {
      const [inboxInfo, inboxStatus, rawHistory] = await Promise.all([
        api.inbox(),
        runJson<InboxStatus>(['inbox', 'status', '--json']),
        runJson<HistoryEntry[]>(['inbox', 'history', '--json'])
      ])
      setInbox(inboxInfo)
      setStatus(inboxStatus)
      setHistory(rawHistory)
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
    }
  }

  useEffect(() => {
    load()
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  const handleFileChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    if (e.target.files) setFiles(Array.from(e.target.files))
  }

  const handleUpload = async () => {
    setUploading(true)
    setMessage(null)
    setError(null)
    try {
      const result = await uploadFiles(files)
      setMessage(`Sent ${result.files ?? files.length} file(s) to the inbox.`)
      setFiles([])
      await load()
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
    } finally {
      setUploading(false)
    }
  }

  const clearHistory = async () => {
    setError(null)
    try {
      await runAction(['inbox', 'history', 'clear', '--json'])
      setMessage('Transfer history cleared.')
      await load()
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
    }
  }

  const sharePath = async (folder: boolean) => {
    const path = await pickPath(folder ? 'folder' : 'file')
    if (!path) return
    setError(null)
    setShareResult(null)
    try {
      const args = ['share', folder ? 'folder' : 'file', path, '--json']
      const result = await runAction(args)
      setShareResult(result.trim())
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
    }
  }

  return (
    <>
      <PageHeader
        eyebrow="Transfer"
        title="File Transfer"
        subtitle="Send files to the inbox on this machine, share local files, and review history."
      />

      {!desktopAvailable() && (
        <StatusBanner kind="message">
          Some actions here require the desktop app. Open hyprshare-desktop to use them.
        </StatusBanner>
      )}
      {error && <StatusBanner kind="error">{error}</StatusBanner>}
      {message && <StatusBanner kind="message">{message}</StatusBanner>}

      <Group title="Receiving inbox">
        {status ? (
          <>
            <Row
              icon="folder"
              title={status.resolved_receive_dir}
              sub={`Configured: ${status.configured_receive_dir}`}
              right={
                <span className={`badge ${status.writable ? 'good' : 'warn'}`}>
                  <span className={`dot ${status.writable ? 'pass' : 'warn'}`} />
                  {status.writable ? 'Ready' : 'Not writable'}
                </span>
              }
            />
            <Row
              icon="network"
              title="Listen address"
              sub={`${status.default_host}:${status.default_port}`}
              value={status.advertised_hosts.length ? `${status.advertised_hosts.length} addresses` : '—'}
            />
          </>
        ) : (
          <EmptyState icon="folder" title="Loading inbox status…" sub="Reading from the CLI." />
        )}
      </Group>

      <Group title="Send files">
        <div className="dropzone">
          <input type="file" multiple onChange={handleFileChange} id="file-input" style={{ display: 'none' }} />
          <div className="drop-icon"><Icon name="upload" /></div>
          <div className="drop-title">Choose files to send</div>
          <div className="drop-hint">They will be placed in the inbox receive directory.</div>
          <label htmlFor="file-input" className="btn"><Icon name="file" /> Select Files</label>
        </div>
      </Group>

      {files.length > 0 && (
        <Group title={`Selected files · ${files.length}`}>
          {files.map((file, index) => (
            <div key={index} className="file-item">
              <div className="file-badge">{file.name.split('.').pop()?.slice(0, 4) || 'file'}</div>
              <div className="row-main">
                <div className="row-title">{file.name}</div>
                <div className="row-sub">{(file.size / 1024).toFixed(1)} KB</div>
              </div>
            </div>
          ))}
          <div className="row" style={{ justifyContent: 'flex-end' }}>
            <ActionButton variant="secondary" icon="trash" onClick={() => setFiles([])} disabled={uploading}>Clear</ActionButton>
            <span style={{ marginLeft: 10 }}>
              <ActionButton icon="send" onClick={handleUpload} disabled={uploading}>
                {uploading ? 'Sending…' : 'Send files'}
              </ActionButton>
            </span>
          </div>
        </Group>
      )}

      <Group title="Share a local file or folder">
        <Row
          icon="share"
          title="Share a path"
          sub="Stages a share session for a file or folder — serve it from the Devices page."
          right={
            <span>
              <ActionButton variant="secondary" icon="file" onClick={() => sharePath(false)}>File</ActionButton>
              <span style={{ marginLeft: 8 }}>
                <ActionButton variant="secondary" icon="folder" onClick={() => sharePath(true)}>Folder</ActionButton>
              </span>
            </span>
          }
        />
        {shareResult && <div className="hint-text mono" style={{ whiteSpace: 'pre-wrap' }}>{shareResult}</div>}
      </Group>

      <Group title={`Transfer history · ${history.length}`}>
        {history.length === 0 ? (
          <EmptyState icon="archive" title="No transfers yet" sub="Files you receive will be listed here." />
        ) : (
          <>
            {history.slice(0, 12).map(entry => (
              <Row
                key={entry.entry_id}
                icon="file"
                title={entry.file_name}
                sub={`${(entry.bytes_received / 1024).toFixed(1)} KB · ${new Date(entry.received_at_unix * 1000).toLocaleString()}`}
                value={entry.sha256 ? <span className="mono">{entry.sha256.slice(0, 10)}…</span> : undefined}
              />
            ))}
            <div className="row" style={{ justifyContent: 'flex-end' }}>
              <ActionButton variant="secondary" icon="trash" onClick={clearHistory}>Clear history</ActionButton>
            </div>
          </>
        )}
      </Group>

      {inbox && <div className="hint-text">Inbox reports {inbox.history_entries} history entries.</div>}
    </>
  )
}

export default FileTransfer
