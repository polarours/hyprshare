// The client for the hyprshare data. Two transports serve the same UI:
//   - in the browser, a REST API started by `hyprshare web serve` (vite proxies
//     /api to it during development);
//   - in the Tauri desktop build there is no server, so calls go through the
//     native `cli` command and the CLI's JSON is shaped to match the REST API.
// The pages only ever see the REST shapes below.

export interface Status {
  version: string
  config_file: string
  state_dir: string
  data_dir: string
}

export interface HealthResult {
  id: string
  status: 'pass' | 'fail' | 'warn' | 'skip'
  severity: string
  summary: string
}

export interface Health {
  checks: number
  pass: number
  fail: number
  warn: number
  skip: number
  results: HealthResult[]
}

export interface Configuration {
  default_scope: string
  discoverability: string
  log_level: string
  receive_dir: string
  allow_fallback_to_obs: boolean
  require_explicit_clipboard_approval: boolean
  default_session_ttl_minutes: number
}

export interface Backend {
  id: string
  summary: string
  launch_supported: boolean
  optional: boolean
  support_tier?: string
  missing?: string[]
}

export interface Peer {
  device_id: string
  device_label: string
}

export interface Inbox {
  receive_dir: string
  history_entries: number
}

// True when running inside the Tauri desktop shell.
function inTauri(): boolean {
  return typeof window !== 'undefined' && '__TAURI_INTERNALS__' in window
}

async function tauriCli(args: string[]): Promise<unknown> {
  // Imported lazily so the browser build does not require the Tauri package.
  const { invoke } = await import('@tauri-apps/api/core')
  const text = await invoke<string>('cli', { args })
  return JSON.parse(text)
}

async function restRequest<T>(path: string): Promise<T> {
  const response = await fetch(path, { headers: { Accept: 'application/json' } })
  if (!response.ok) {
    throw new Error(`the server answered with status ${response.status}`)
  }
  return (await response.json()) as T
}

// --- shapers: CLI JSON -> REST shape ----------------------------------------

type AnyObj = Record<string, any>

function shapeStatus(raw: AnyObj): Status {
  const paths = raw.paths ?? {}
  return {
    version: raw.version ?? '',
    config_file: paths.config_file ?? raw.config_file ?? '',
    state_dir: paths.state_dir ?? raw.state_dir ?? '',
    data_dir: paths.data_dir ?? raw.data_dir ?? ''
  }
}

function shapeHealth(raw: AnyObj): Health {
  // `doctor --json` returns { facts, results: [...] }. The REST endpoint also
  // carries counters, computed here from the same results.
  const results: HealthResult[] = (raw.results ?? raw.checks ?? []).map(
    (c: AnyObj) => ({
      id: c.id ?? '',
      status: c.status ?? 'skip',
      severity: c.severity ?? 'info',
      summary: c.summary ?? ''
    })
  )
  const count = (s: string) => results.filter((r) => r.status === s).length
  return {
    checks: results.length,
    pass: count('pass'),
    fail: count('fail'),
    warn: count('warn'),
    skip: count('skip'),
    results
  }
}

function shapeConfig(raw: AnyObj): Configuration {
  return {
    default_scope: raw.general?.default_scope ?? raw.default_scope ?? '',
    discoverability: raw.general?.discoverability ?? raw.discoverability ?? '',
    log_level: raw.general?.log_level ?? raw.log_level ?? '',
    receive_dir: raw.files?.receive_dir ?? raw.receive_dir ?? '',
    allow_fallback_to_obs: raw.screen?.allow_fallback_to_obs ?? raw.allow_fallback_to_obs ?? false,
    require_explicit_clipboard_approval:
      raw.security?.require_explicit_clipboard_approval ??
      raw.require_explicit_clipboard_approval ?? false,
    default_session_ttl_minutes:
      raw.security?.default_session_ttl_minutes ?? raw.default_session_ttl_minutes ?? 0
  }
}

function shapeBackends(raw: unknown): { backends: Backend[] } {
  const list = Array.isArray(raw) ? raw : (raw as AnyObj)?.backends ?? []
  return {
    backends: list.map((b: AnyObj) => ({
      id: b.id ?? '',
      summary: b.summary ?? '',
      // The CLI reports `ready` plus `support_tier`/`missing`; older REST
      // payloads used `launch_supported`/`optional`. Accept both.
      launch_supported: b.ready ?? b.launch_supported ?? false,
      optional: b.support_tier ? b.support_tier !== 'primary' : (b.optional ?? false),
      support_tier: b.support_tier ?? '',
      missing: Array.isArray(b.missing) ? b.missing : []
    }))
  }
}

function shapePeers(raw: unknown): { peers: Peer[] } {
  const list = Array.isArray(raw) ? raw : (raw as AnyObj)?.peers ?? []
  return {
    peers: list.map((p: AnyObj) => ({
      device_id: p.device_id ?? p.peer_id ?? '',
      device_label: p.device_label ?? p.label ?? ''
    }))
  }
}

// Short-lived cache for read-only CLI queries. The CLI now runs ~10x faster
// after removing the login shell, but spawning several processes on every
// navigation is still wasteful; a few seconds of TTL removes that cost without
// showing stale data for long.
const readCache = new Map<string, { at: number; value: unknown }>()
const DEFAULT_TTL_MS = 3000

export function clearCommandCache(): void {
  readCache.clear()
}

async function desktopGetCached<T>(
  args: string[],
  shape: (raw: AnyObj) => T,
  ttlMs = DEFAULT_TTL_MS,
): Promise<T> {
  const key = args.join(' ')
  const hit = readCache.get(key)
  if (hit && Date.now() - hit.at < ttlMs) {
    return shape(hit.value as AnyObj)
  }
  const value = await tauriCli(args)
  readCache.set(key, { at: Date.now(), value })
  return shape(value as AnyObj)
}

export const api = {
  status: (): Promise<Status> =>
    inTauri()
      ? desktopGetCached(['status', '--json'], shapeStatus)
      : restRequest<Status>('/api/status'),

  health: (): Promise<Health> =>
    inTauri()
      ? desktopGetCached(['doctor', '--json'], shapeHealth)
      : restRequest<Health>('/api/health'),

  config: (): Promise<Configuration> =>
    inTauri()
      ? desktopGetCached(['config', 'show', '--json'], shapeConfig)
      : restRequest<Configuration>('/api/config'),

  backends: (): Promise<{ backends: Backend[] }> =>
    inTauri()
      ? desktopGetCached(['backend', 'list', '--json'], shapeBackends)
      : restRequest<{ backends: Backend[] }>('/api/backends'),

  peers: (): Promise<{ peers: Peer[] }> =>
    inTauri()
      ? desktopGetCached(['peer', 'list', '--json'], shapePeers)
      : restRequest<{ peers: Peer[] }>('/api/peers'),

  inbox: async (): Promise<Inbox> => {
    if (!inTauri()) {
      return restRequest<Inbox>('/api/inbox')
    }
    // The CLI reports history and receive dir separately; combine them.
    const [history, configRaw] = await Promise.all([
      tauriCli(['inbox', 'history', '--json']),
      tauriCli(['config', 'show', '--json'])
    ])
    const h = history as AnyObj
    const entries = Array.isArray(history)
      ? history.length
      : Array.isArray(h.entries)
        ? h.entries.length
        : h.count ?? h.history_entries ?? 0
    return {
      receive_dir: shapeConfig(configRaw as AnyObj).receive_dir,
      history_entries: Number(entries)
    }
  }
}

// Upload used by FileTransfer.tsx. In the browser it posts multipart to the
// REST server; in Tauri the bytes go through the native command.
export async function uploadFiles(files: File[]): Promise<{ files: number }> {
  if (inTauri()) {
    const { invoke } = await import('@tauri-apps/api/core')
    const payload = await Promise.all(
      files.map(async (f) => ({
        name: f.name,
        bytes: Array.from(new Uint8Array(await f.arrayBuffer()))
      }))
    )
    const count = await invoke<number>('upload_files', { files: payload })
    return { files: count }
  }
  const form = new FormData()
  files.forEach((f) => form.append('files', f))
  const response = await fetch('/api/upload', { method: 'POST', body: form })
  if (!response.ok) {
    const result = (await response.json()) as { error?: string }
    throw new Error(result.error ?? `the server answered with status ${response.status}`)
  }
  return (await response.json()) as { files: number }
}

// ---------------------------------------------------------------------------
// Generic CLI runner. Every desktop action is a CLI command; the pages use
// this to read structured data and to trigger operations. These actions are
// desktop-only: the browser REST server has no equivalent write routes.
// ---------------------------------------------------------------------------

export function desktopAvailable(): boolean {
  return inTauri()
}

/** Actually spawn the CLI. Only the cache-aware wrappers should call this. */
async function invokeCli(args: string[]): Promise<string> {
  if (!inTauri()) {
    throw new Error('This action is available in the desktop app only.')
  }
  const { invoke } = await import('@tauri-apps/api/core')
  return invoke<string>('cli', { args })
}

/**
 * Read a CLI command's text output. Read-only commands are cached briefly so
 * navigating between pages does not respawn the same process; pass
 * cacheMs = 0 to force a fresh run (used by explicit refresh buttons).
 */
export async function runCommand(args: string[], cacheMs: number = DEFAULT_TTL_MS): Promise<string> {
  if (cacheMs > 0) {
    const key = args.join(' ')
    const hit = readCache.get(key)
    if (hit && Date.now() - hit.at < cacheMs) {
      return hit.value as string
    }
  }
  const result = await invokeCli(args)
  if (cacheMs > 0) {
    readCache.set(args.join(' '), { at: Date.now(), value: result })
  }
  return result
}

/** Run a state-changing CLI action, bypassing and then invalidating the cache. */
export async function runAction(args: string[]): Promise<string> {
  const result = await invokeCli(args)
  clearCommandCache()
  return result
}

export interface ServeSessionInfo {
  token: string
  port: number
  pid: number
  address: string
}

/** Start a background session server with a fresh token (desktop only). */
export async function serveSession(sessionId: string, port: number): Promise<ServeSessionInfo> {
  if (!inTauri()) {
    throw new Error('Serving a session requires the desktop app.')
  }
  const { invoke } = await import('@tauri-apps/api/core')
  return invoke<ServeSessionInfo>('serve_session', { sessionId, port })
}

/**
 * Pick a file or folder path. In the desktop app this opens the native file
 * dialog; in the browser it falls back to a prompt so pages keep working.
 */
export async function pickPath(kind: 'file' | 'folder'): Promise<string | null> {
  if (inTauri()) {
    const { open } = await import('@tauri-apps/plugin-dialog')
    const picked = await open({
      directory: kind === 'folder',
      multiple: false,
      title: kind === 'folder' ? 'Choose a folder to share' : 'Choose a file to share',
    })
    return typeof picked === 'string' ? picked : null
  }
  const answer = window.prompt(
    kind === 'folder' ? 'Folder path to share:' : 'File path to share:'
  )
  return answer && answer.trim() ? answer.trim() : null
}

/** Run a CLI command with --json and parse the result (cached, read-only). */
export async function runJson<T = unknown>(args: string[], cacheMs: number = DEFAULT_TTL_MS): Promise<T> {
  const text = await runCommand(args, cacheMs)
  try {
    return JSON.parse(text) as T
  } catch {
    // Some commands print an empty line with --json; treat as empty list.
    return [] as unknown as T
  }
}
