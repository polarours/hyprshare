import { BrowserRouter as Router, Routes, Route, NavLink } from 'react-router-dom'
import Dashboard from './pages/Dashboard'
import FileTransfer from './pages/FileTransfer'
import ClipboardPage from './pages/Clipboard'
import Devices from './pages/Devices'
import Sessions from './pages/Sessions'
import Operations from './pages/Operations'
import Services from './pages/Services'
import Tools from './pages/Tools'
import DeviceDetail from './pages/DeviceDetail'
import SettingsPage from './pages/Settings'
import { Icon } from './components/Icon'
import { ThemeProvider, useTheme, type ThemePreference } from './theme'
import './App.css'

function ThemeToggle() {
  const { resolved, toggle } = useTheme()
  return (
    <button
      className="theme-toggle"
      onClick={toggle}
      title={resolved === 'dark' ? 'Switch to light appearance' : 'Switch to dark appearance'}
      aria-label="Toggle appearance"
    >
      <Icon name={resolved === 'dark' ? 'sun' : 'moon'} />
    </button>
  )
}

function AppearancePicker() {
  const { preference, setPreference } = useTheme()
  const options: { value: ThemePreference; icon: 'sun' | 'moon' | 'system'; label: string }[] = [
    { value: 'light', icon: 'sun', label: 'Light' },
    { value: 'dark', icon: 'moon', label: 'Dark' },
    { value: 'system', icon: 'system', label: 'System' },
  ]
  return (
    <div className="appearance">
      <div className="sidebar-label">Appearance</div>
      <div className="segmented" role="group" aria-label="Appearance">
        {options.map(option => (
          <button
            key={option.value}
            className={'segment' + (preference === option.value ? ' active' : '')}
            onClick={() => setPreference(option.value)}
            title={`${option.label} appearance`}
            aria-pressed={preference === option.value}
          >
            <Icon name={option.icon} />
          </button>
        ))}
      </div>
    </div>
  )
}

function Titlebar() {
  // macOS-style window controls. Decorative on purpose: a second, non-native
  // close path wired into the Tauri window would be surprising.
  return (
    <div className="titlebar">
      <div className="traffic-lights" aria-hidden="true">
        <span className="traffic close" />
        <span className="traffic min" />
        <span className="traffic max" />
      </div>
      <div className="titlebar-title">hyprshare</div>
      <ThemeToggle />
    </div>
  )
}

function Shell() {
  return (
    <div className="app">
      <Titlebar />
      <div className="main-layout">
        <aside className="sidebar">
          <div className="brand">
            <div className="brand-icon"><Icon name="share" /></div>
            <div className="brand-name">hyprshare</div>
          </div>
          <div className="sidebar-label">Workspace</div>
          <nav>
            <NavLink to="/" end className={({ isActive }) => 'nav-item' + (isActive ? ' active' : '')}>
              <Icon name="grid" /><span>Dashboard</span>
            </NavLink>
            <NavLink to="/transfer" className={({ isActive }) => 'nav-item' + (isActive ? ' active' : '')}>
              <Icon name="transfer" /><span>File Transfer</span>
            </NavLink>
            <NavLink to="/clipboard" className={({ isActive }) => 'nav-item' + (isActive ? ' active' : '')}>
              <Icon name="clipboard" /><span>Clipboard</span>
            </NavLink>
            <NavLink to="/devices" className={({ isActive }) => 'nav-item' + (isActive ? ' active' : '')}>
              <Icon name="devices" /><span>Devices</span>
            </NavLink>
            <NavLink to="/sessions" className={({ isActive }) => 'nav-item' + (isActive ? ' active' : '')}>
              <Icon name="share" /><span>Sessions</span>
            </NavLink>
            <NavLink to="/operations" className={({ isActive }) => 'nav-item' + (isActive ? ' active' : '')}>
              <Icon name="shield" /><span>Operations</span>
            </NavLink>
            <NavLink to="/services" className={({ isActive }) => 'nav-item' + (isActive ? ' active' : '')}>
              <Icon name="power" /><span>Services</span>
            </NavLink>
            <NavLink to="/tools" className={({ isActive }) => 'nav-item' + (isActive ? ' active' : '')}>
              <Icon name="tools" /><span>Tools</span>
            </NavLink>
            <NavLink to="/settings" className={({ isActive }) => 'nav-item' + (isActive ? ' active' : '')}>
              <Icon name="settings" /><span>Settings</span>
            </NavLink>
          </nav>
          <div className="sidebar-bottom">
            <AppearancePicker />
            <div className="sidebar-foot">Share across your devices,<br />right from Hyprland.</div>
          </div>
        </aside>
        <main className="content">
          <div className="content-inner">
            <Routes>
              <Route path="/" element={<Dashboard />} />
              <Route path="/transfer" element={<FileTransfer />} />
              <Route path="/clipboard" element={<ClipboardPage />} />
              <Route path="/devices" element={<Devices />} />
              <Route path="/sessions" element={<Sessions />} />
              <Route path="/operations" element={<Operations />} />
              <Route path="/services" element={<Services />} />
              <Route path="/tools" element={<Tools />} />
              <Route path="/this-device" element={<DeviceDetail />} />
              <Route path="/settings" element={<SettingsPage />} />
            </Routes>
          </div>
        </main>
      </div>
    </div>
  )
}

function App() {
  return (
    <ThemeProvider>
      <Router>
        <Shell />
      </Router>
    </ThemeProvider>
  )
}

export default App
