import { createContext, useContext, useEffect, useState, type ReactNode } from 'react'

export type ThemePreference = 'light' | 'dark' | 'system'
export type ResolvedTheme = 'light' | 'dark'

interface ThemeContextValue {
  preference: ThemePreference
  resolved: ResolvedTheme
  setPreference: (value: ThemePreference) => void
  toggle: () => void
}

const ThemeContext = createContext<ThemeContextValue | null>(null)
const STORAGE_KEY = 'hyprshare.theme'

function readPreference(): ThemePreference {
  try {
    const stored = localStorage.getItem(STORAGE_KEY)
    if (stored === 'light' || stored === 'dark' || stored === 'system') {
      return stored
    }
  } catch {
    // Storage can be unavailable in private browsing; fall back to system.
  }
  return 'system'
}

function systemPrefersDark(): boolean {
  return typeof window !== 'undefined' &&
    window.matchMedia('(prefers-color-scheme: dark)').matches
}

export function ThemeProvider({ children }: { children: ReactNode }) {
  const [preference, setPreferenceState] = useState<ThemePreference>(readPreference)
  const [systemDark, setSystemDark] = useState<boolean>(systemPrefersDark)

  // Keep in sync with the OS setting while "system" is selected.
  useEffect(() => {
    const query = window.matchMedia('(prefers-color-scheme: dark)')
    const onChange = (event: MediaQueryListEvent) => setSystemDark(event.matches)
    query.addEventListener('change', onChange)
    return () => query.removeEventListener('change', onChange)
  }, [])

  const resolved: ResolvedTheme =
    preference === 'system' ? (systemDark ? 'dark' : 'light') : preference

  useEffect(() => {
    document.documentElement.dataset.theme = resolved
    try {
      localStorage.setItem(STORAGE_KEY, preference)
    } catch {
      // Ignore write failures; the theme still applies for this session.
    }
  }, [preference, resolved])

  const setPreference = (value: ThemePreference) => setPreferenceState(value)
  const toggle = () => setPreferenceState(resolved === 'dark' ? 'light' : 'dark')

  return (
    <ThemeContext.Provider value={{ preference, resolved, setPreference, toggle }}>
      {children}
    </ThemeContext.Provider>
  )
}

export function useTheme(): ThemeContextValue {
  const value = useContext(ThemeContext)
  if (!value) {
    throw new Error('useTheme must be used inside a ThemeProvider')
  }
  return value
}
