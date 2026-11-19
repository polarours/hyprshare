import { useState, useEffect } from 'react'
import { runCommand, runJson, runAction, desktopAvailable } from '../api'
import { PageHeader, Group, Row, StatusBanner, EmptyState, ActionButton } from '../components/ui'

interface HistoryItem {
  content: string
  mime_type: string
}

function Clipboard() {
  const [content, setContent] = useState('')
  const [history, setHistory] = useState<HistoryItem[]>([])
  const [status, setStatus] = useState<string>('—')
  const [message, setMessage] = useState<string | null>(null)
  const [error, setError] = useState<string | null>(null)
  const [pushTarget, setPushTarget] = useState('')
  const [pullSource, setPullSource] = useState('')

  const load = async () => {
    try {
      const [rawHistory, rawStatus] = await Promise.all([
        runJson<HistoryItem[]>(['clipboard-sync', 'history', '--json']),
        runCommand(['clipboard-sync', 'status'])
      ])
      setHistory(rawHistory)
      setStatus(rawStatus.trim())
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

  const readClipboard = () => act(['clipboard', 'read', '--json'], 'Read clipboard')
  const writeClipboard = () => {
    if (!content.trim()) {
      setError('Enter some text to write.')
      return
    }
    act(['clipboard', 'write', '--text', content], 'Wrote to clipboard')
  }
  const copyItem = (item: HistoryItem) => {
    setContent(item.content)
    act(['clipboard', 'write', '--text', item.content], 'Restored from history')
  }

  return (
    <>
      <PageHeader
        eyebrow="Clipboard"
        title="Clipboard"
        subtitle="Read and write the system clipboard, keep a history, and exchange it with peers."
      />

      {!desktopAvailable() && (
        <StatusBanner kind="message">Clipboard actions require the desktop app.</StatusBanner>
      )}
      {error && <StatusBanner kind="error">{error}</StatusBanner>}
      {message && <StatusBanner kind="message">{message}</StatusBanner>}

      <Group title="Clipboard content">
        <div className="row" style={{ flexDirection: 'column', alignItems: 'stretch', gap: 12 }}>
          <textarea
            className="text-input"
            placeholder="Type text to write to the clipboard, or read from it…"
            value={content}
            onChange={e => setContent(e.target.value)}
            rows={5}
          />
          <div style={{ display: 'flex', gap: 8, justifyContent: 'flex-end' }}>
            <ActionButton variant="secondary" icon="copy" onClick={readClipboard}>Read</ActionButton>
            <ActionButton icon="clipboard" onClick={writeClipboard}>Write</ActionButton>
          </div>
        </div>
      </Group>

      <Group title="Cross-device sync">
        <Row icon="network" title="Service status" sub={status} />
        <div className="row" style={{ flexDirection: 'column', alignItems: 'stretch', gap: 10 }}>
          <input
            className="text-input"
            placeholder="Peer address (host:port) to push to"
            value={pushTarget}
            onChange={e => setPushTarget(e.target.value)}
          />
          <div style={{ display: 'flex', gap: 8, justifyContent: 'flex-end', flexWrap: 'wrap' }}>
            <ActionButton
              variant="secondary"
              icon="send"
              disabled={!pushTarget.trim() || !content.trim()}
              onClick={() => act(['clipboard-sync', 'send', pushTarget.trim()], 'Pushed to peer')}
            >
              Push to peer
            </ActionButton>
            <ActionButton
              variant="secondary"
              icon="refresh"
              disabled={!pullSource.trim()}
              onClick={() => act(['clipboard-sync', 'receive', pullSource.trim()], 'Pulled from peer')}
            >
              Pull from peer
            </ActionButton>
          </div>
          <input
            className="text-input"
            placeholder="Peer address (host:port) to pull from"
            value={pullSource}
            onChange={e => setPullSource(e.target.value)}
          />
        </div>
      </Group>

      <Group title={`History · ${history.length}`}>
        {history.length === 0 ? (
          <EmptyState icon="clipboard" title="No clipboard history yet" sub="Values pushed by commands will be kept here." />
        ) : (
          history.slice(0, 12).map((item, index) => (
            <Row
              key={index}
              icon="clipboard"
              title={item.content.length > 80 ? item.content.slice(0, 80) + '…' : item.content}
              sub={item.mime_type}
              right={
                <ActionButton variant="secondary" icon="copy" onClick={() => copyItem(item)}>Restore</ActionButton>
              }
            />
          ))
        )}
      </Group>
    </>
  )
}

export default Clipboard
