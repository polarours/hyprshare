import type { ReactNode } from 'react'
import { Icon, type IconName } from './Icon'

export function PageHeader({ eyebrow, title, subtitle }: { eyebrow: string; title: string; subtitle: string }) {
  return (
    <>
      <div className="page-eyebrow">{eyebrow}</div>
      <h1 className="page-title">{title}</h1>
      <p className="page-subtitle">{subtitle}</p>
    </>
  )
}

export function Group({ title, children, hint }: { title: string; children: ReactNode; hint?: string }) {
  return (
    <div className="group">
      <div className="group-title">{title}</div>
      <div className="group-body">{children}</div>
      {hint && <div className="hint-text">{hint}</div>}
    </div>
  )
}

export function Row({
  icon,
  title,
  sub,
  right,
  value,
}: {
  icon?: IconName
  title: ReactNode
  sub?: ReactNode
  right?: ReactNode
  value?: ReactNode
}) {
  return (
    <div className="row">
      {icon && <div className="row-icon"><Icon name={icon} /></div>}
      <div className="row-main">
        <div className="row-title">{title}</div>
        {sub !== undefined && <div className="row-sub">{sub}</div>}
      </div>
      {value !== undefined && <div className="row-value">{value}</div>}
      {right}
    </div>
  )
}

export function StatusBanner({ kind, children }: { kind: 'error' | 'message'; children: ReactNode }) {
  return <div className={`notice ${kind}`}>{children}</div>
}

export function Loading({ label = 'Loading…' }: { label?: string }) {
  return (
    <div className="loading">
      <div className="spinner" />
      <div>{label}</div>
    </div>
  )
}

export function EmptyState({ icon, title, sub }: { icon: IconName; title: string; sub: string }) {
  return (
    <Row
      icon={icon}
      title={title}
      sub={sub}
    />
  )
}

export function ActionButton({
  icon,
  children,
  onClick,
  disabled,
  variant = 'primary',
}: {
  icon?: IconName
  children: ReactNode
  onClick: () => void
  disabled?: boolean
  variant?: 'primary' | 'secondary'
}) {
  return (
    <button
      className={'btn' + (variant === 'secondary' ? ' btn-secondary' : '')}
      onClick={onClick}
      disabled={disabled}
    >
      {icon && <Icon name={icon} />}
      {children}
    </button>
  )
}
