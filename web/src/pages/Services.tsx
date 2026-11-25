import { useState, useEffect } from 'react'
import { runJson, runAction, pickPath, desktopAvailable } from '../api'
import { PageHeader, Group, Row, StatusBanner, EmptyState, ActionButton } from '../components/ui'

interface DaemonStatus { running: boolean; pid?: number; socket?: string }
interface RemoteStatus { status: string; pid?: number; port?: number; input_events?: number; input_backend?: string }
interface CloudStatus { enabled: boolean; provider?: string; status?: string }
interface CloudEntry { name?: string; path?: string; size?: number }
interface ProfileReport { total_duration_us: number; total_bytes: number; entries: Array<{ name: string; duration_us: number; running?: boolean }> }
interface CompatProfile { id: string; display_name?: string; launch_commands?: string[]; notes?: string[] }

function Services() {
  const [daemon, setDaemon] = useState<DaemonStatus | null>(null)
  const [remote, setRemote] = useState<RemoteStatus | null>(null)
  const [cloud, setCloud] = useState<CloudStatus | null>(null)
  const [cloudFiles, setCloudFiles] = useState<CloudEntry[]>([])
  const [profiling, setProfiling] = useState<ProfileReport | null>(null)
  const [compat, setCompat] = useState<CompatProfile | null>(null)
  const [error, setError] = useState<string | null>(null)
  const [message, setMessage] = useState<string | null>(null)

  const [mobileDevice, setMobileDevice] = useState('')
  const [remotePort, setRemotePort] = useState('28557')
  const [cloudProvider, setCloudProvider] = useState('localdisk')
  const [cloudType, setCloudType] = useState('local')
  const [cloudEndpoint, setCloudEndpoint] = useState('')
  const [remotePath, setRemotePath] = useState('')
  const [compatApp, setCompatApp] = useState('zoom')
  const [batchCommands, setBatchCommands] = useState('echo hello')
  const [profileName, setProfileName] = useState('session')

  const read = async <T,>(args: string[]): Promise<T | null> => {
    try { return await runJson<T>(args, 0) } catch { return null }
  }

  const refresh = async () => {
    setError(null)
    const [d, r, c, p] = await Promise.all([
      read<DaemonStatus>(['daemon', 'status', '--json']),
      read<RemoteStatus>(['remote', 'status', '--json']),
      read<CloudStatus>(['cloud', 'status', '--json']),
      read<ProfileReport>(['profiling', 'report', '--json']),
    ])
    setDaemon(d)
    setRemote(r)
    setCloud(c)
    setProfiling(p)
    if (c?.enabled) {
      const files = await read<CloudEntry[]>(['cloud', 'list', '.', '--json'])
      setCloudFiles(Array.isArray(files) ? files : [])
    } else {
      setCloudFiles([])
    }
  }

  useEffect(() => { refresh() /* eslint-disable-line react-hooks/exhaustive-deps */ }, [])

  const act = async (args: string[], success: string) => {
    setError(null)
    setMessage(null)
    try {
      const out = await runAction(args)
      setMessage(success + (out.trim() ? ` — ${out.trim().slice(0, 160)}` : ''))
      await refresh()
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
    }
  }

  const sendMobileFile = async () => {
    setError(null)
    if (!mobileDevice.trim()) {
      setError('Enter a device id first.')
      return
    }
    const file = await pickPath('file')
    if (!file) return
    await act(['mobile', 'send', mobileDevice.trim(), file], `Sent to ${mobileDevice.trim()}`)
  }

  const uploadCloud = async () => {
    setError(null)
    if (!remotePath.trim()) {
      setError('Enter a remote path first.')
      return
    }
    const file = await pickPath('file')
    if (!file) return
    await act(['cloud', 'upload', file, remotePath.trim(), '--json'], 'Uploaded')
  }

  const downloadCloud = async () => {
    setError(null)
    if (!remotePath.trim()) {
      setError('Enter a remote path first.')
      return
    }
    const dir = await pickPath('folder')
    if (!dir) return
    const name = remotePath.trim().split('/').pop() || 'download'
    await act(['cloud', 'download', remotePath.trim(), `${dir}/${name}`, '--json'], 'Downloaded')
  }

  return (
    <>
      <PageHeader
        eyebrow="Connectivity"
        title="Services"
        subtitle="Daemon, mobile transfer, remote control, cloud storage, app compatibility and profiling — every action runs the real CLI."
      />

      {!desktopAvailable() && (
        <StatusBanner kind="message">These services can only be controlled from the desktop app.</StatusBanner>
      )}
      {error && <StatusBanner kind="error">{error}</StatusBanner>}
      {message && <StatusBanner kind="message">{message}</StatusBanner>}

      <Group title="Daemon" hint="The background service that owns inbox listeners and discovery.">
        <Row
          icon="power"
          title={daemon?.running ? 'Daemon is running' : 'Daemon is not running'}
          sub={daemon?.socket ? `Socket: ${daemon.socket}` : 'Start it to receive files in the background.'}
          value={daemon?.pid ? `pid ${daemon.pid}` : undefined}
          right={
            <ActionButton
              variant={daemon?.running ? 'secondary' : 'primary'}
              icon={daemon?.running ? 'stop' : 'play'}
              onClick={() => act([daemon?.running ? 'daemon stop' : 'daemon start'].join(' ').split(' '), daemon?.running ? 'Daemon stopped' : 'Daemon started')}
            >
              {daemon?.running ? 'Stop' : 'Start'}
            </ActionButton>
          }
        />
      </Group>

      <Group title="Mobile transfer" hint="Send a file to a paired phone or tablet.">
        <Row
          icon="device"
          title="Send a file to a device"
          sub="Choose a file with the native picker after entering the device id."
          right={
            <div className="row-inline-inputs">
              <input
                className="text-input"
                value={mobileDevice}
                onChange={e => setMobileDevice(e.target.value)}
                placeholder="device id"
                aria-label="Device id"
              />
              <ActionButton variant="secondary" icon="send" onClick={sendMobileFile}>Choose &amp; send</ActionButton>
            </div>
          }
        />
      </Group>

      <Group title="Remote control" hint="A detached server that records input events from a phone.">
        <Row
          icon="network"
          title={remote?.status === 'running' ? 'Remote server is running' : 'Remote server is stopped'}
          sub={`Recorded events: ${remote?.input_events ?? 0} · injection backend: ${remote?.input_backend ?? 'unknown'}`}
          value={remote?.port ? `:${remote.port}` : undefined}
          right={
            <div className="row-inline-inputs">
              <input
                className="text-input"
                value={remotePort}
                onChange={e => setRemotePort(e.target.value)}
                placeholder="port"
                aria-label="Remote port"
              />
              <ActionButton
                variant={remote?.status === 'running' ? 'secondary' : 'primary'}
                icon={remote?.status === 'running' ? 'stop' : 'play'}
                onClick={() => remote?.status === 'running'
                  ? act(['remote', 'stop'], 'Remote server stopped')
                  : act(['remote', 'serve', '--port', remotePort], 'Remote server started')}
              >
                {remote?.status === 'running' ? 'Stop' : 'Start'}
              </ActionButton>
            </div>
          }
        />
      </Group>

      <Group title="Cloud storage" hint="Local-directory and HTTP providers; no account required for local storage.">
        <Row
          icon="network"
          title={cloud?.enabled ? `Provider: ${cloud.provider ?? 'configured'}` : 'No provider configured'}
          sub="Configure a local store with an absolute path, or an HTTP endpoint."
          right={
            <ActionButton variant="secondary" icon="refresh" onClick={() =>
              act(['cloud', 'configure', cloudProvider, '--type', cloudType, '--endpoint', cloudEndpoint, '--json'], 'Provider configured')
            }>Configure</ActionButton>
          }
        />
        <div className="row-inline-inputs row-inline-padded">
          <input className="text-input" value={cloudProvider} onChange={e => setCloudProvider(e.target.value)} placeholder="provider" aria-label="Cloud provider" />
          <input className="text-input" value={cloudType} onChange={e => setCloudType(e.target.value)} placeholder="local | http" aria-label="Cloud type" />
          <input className="text-input" value={cloudEndpoint} onChange={e => setCloudEndpoint(e.target.value)} placeholder="local path or URL" aria-label="Cloud endpoint" />
        </div>
        <Row
          icon="upload"
          title="Upload / download"
          sub="Pick a local file for upload, or a folder to download into."
          right={
            <div className="row-inline-inputs">
              <input className="text-input" value={remotePath} onChange={e => setRemotePath(e.target.value)} placeholder="remote path" aria-label="Remote path" />
              <ActionButton variant="secondary" icon="upload" onClick={uploadCloud}>Upload</ActionButton>
              <ActionButton variant="secondary" icon="transfer" onClick={downloadCloud}>Download</ActionButton>
            </div>
          }
        />
        {cloudFiles.length === 0 ? (
          <EmptyState icon="folder" title="No remote files listed" sub="Configure a provider and upload a file to see it here." />
        ) : (
          cloudFiles.map((file, i) => (
            <Row key={`${file.name ?? 'file'}-${i}`} icon="file" title={file.name ?? file.path ?? `file ${i + 1}`} value={file.size ? `${file.size} B` : undefined} />
          ))
        )}
      </Group>

      <Group title="App compatibility" hint="Profiles for apps with broken direct Wayland capture.">
        <Row
          icon="grid"
          title={compat ? `${compat.display_name ?? compat.id}` : 'Check an app'}
          sub={compat?.notes?.join(' · ') ?? 'Known profiles: zoom, wemeet, browser-webrtc.'}
          right={
            <div className="row-inline-inputs">
              <input className="text-input" value={compatApp} onChange={e => setCompatApp(e.target.value)} placeholder="app name" aria-label="App name" />
              <ActionButton variant="secondary" icon="search" onClick={async () => {
                setError(null)
                try { setCompat(await runJson<CompatProfile>(['compat', 'show', compatApp, '--json'], 0)) }
                catch (cause) { setError(cause instanceof Error ? cause.message : String(cause)) }
              }}>Inspect</ActionButton>
              <ActionButton variant="secondary" icon="play" onClick={() =>
                act(['app', 'launch', compatApp, '--dry-run'], 'Launch plan ready')
              }>Dry run</ActionButton>
            </div>
          }
        />
      </Group>

      <Group title="Batch commands" hint="Run several shell commands in one click, in order.">
        <div className="row-inline-padded">
          <textarea
            className="text-input"
            value={batchCommands}
            onChange={e => setBatchCommands(e.target.value)}
            rows={3}
            placeholder="one command per line"
            aria-label="Batch commands"
          />
        </div>
        <Row
          icon="tools"
          title="Run the commands"
          sub="Each line is a complete shell command; failures are reported with their exit code."
          right={
            <ActionButton variant="secondary" icon="play" onClick={() =>
              act(['batch', 'run-commands', ...batchCommands.split('\n').map(s => s.trim()).filter(Boolean)], 'Batch finished')
            }>Run batch</ActionButton>
          }
        />
      </Group>

      <Group title="Profiling" hint="Timed sessions shared with the CLI across processes.">
        <Row
          icon="record"
          title={profiling?.entries?.length ? `${profiling.entries.length} recorded session(s)` : 'No profiling sessions'}
          sub={profiling ? `Total ${profiling.total_duration_us} us · ${profiling.total_bytes} bytes` : 'Start a named timer, then stop it later.'}
          right={
            <div className="row-inline-inputs">
              <input className="text-input" value={profileName} onChange={e => setProfileName(e.target.value)} placeholder="session name" aria-label="Profile name" />
              <ActionButton variant="secondary" icon="record" onClick={() => act(['profiling', 'start', profileName, '--json'], 'Timer started')}>Start</ActionButton>
              <ActionButton variant="secondary" icon="stop" onClick={() => act(['profiling', 'stop', profileName, '--json'], 'Timer stopped')}>Stop</ActionButton>
              <ActionButton variant="secondary" icon="trash" onClick={() => act(['profiling', 'clear', '--json'], 'Profiling cleared')}>Clear</ActionButton>
            </div>
          }
        />
      </Group>
    </>
  )
}

export default Services
