// Data model for the TUI: one subprocess batch of `hyprshare --json` calls.

use serde::Deserialize;

#[derive(Deserialize, Default, Clone)]
pub struct Snapshot {
    #[serde(default)]
    pub cpu: f64,
    #[serde(default)]
    pub memory: f64,
    #[serde(default)]
    pub disk: f64,
}

// `doctor --json` returns { facts, checks: [{ id, status, summary, … }] }.
// The REST endpoint returns counters; derive them here from the check list.
#[derive(Deserialize, Default, Clone)]
pub struct HealthResult {
    #[serde(default)]
    pub id: String,
    #[serde(default)]
    pub status: String,
    #[serde(default)]
    pub summary: String,
}

#[derive(Deserialize, Default, Clone)]
pub struct DoctorJson {
    #[serde(default)]
    pub checks: Vec<HealthResult>,
}

#[derive(Default, Clone)]
pub struct Health {
    pub checks: usize,
    pub pass: usize,
    pub fail: usize,
    pub warn: usize,
    pub results: Vec<HealthResult>,
}

impl Health {
    fn from_doctor(doctor: DoctorJson) -> Self {
        let mut health = Health {
            checks: doctor.checks.len(),
            results: doctor.checks,
            ..Default::default()
        };
        for result in &health.results {
            match result.status.as_str() {
                "pass" => health.pass += 1,
                "fail" => health.fail += 1,
                "warn" => health.warn += 1,
                _ => {}
            }
        }
        health
    }
}

#[derive(Deserialize, Default, Clone)]
pub struct PeerSelf {
    #[allow(dead_code)]
    #[serde(default)]
    pub device_id: String,
    #[serde(default)]
    pub device_label: String,
}

#[derive(Deserialize, Default, Clone)]
pub struct StatusPaths {
    #[serde(default)]
    pub state_dir: String,
}

#[derive(Deserialize, Default, Clone)]
pub struct Status {
    #[serde(default)]
    pub version: String,
    #[serde(default)]
    pub paths: StatusPaths,
}

#[derive(Deserialize, Default, Clone)]
pub struct Inbox {
    #[serde(default)]
    pub resolved_receive_dir: String,
    #[serde(default)]
    pub default_host: String,
    #[serde(default)]
    pub default_port: u16,
    #[serde(default)]
    pub writable: bool,
}

#[derive(Deserialize, Default, Clone)]
pub struct HistoryEntry {
    #[serde(default)]
    pub file_name: String,
    #[serde(default)]
    pub bytes_received: u64,
    #[serde(default)]
    pub received_at_unix: i64,
}

#[derive(Deserialize, Default, Clone)]
pub struct ClipboardItem {
    #[serde(default)]
    pub content: String,
}

// `peer list --json`
#[derive(Deserialize, Default, Clone)]
pub struct PeerEntry {
    #[serde(default)]
    pub device_id: String,
    #[serde(default)]
    pub device_label: String,
    #[serde(default)]
    pub added_at_unix: i64,
}

// `session list --json`
#[derive(Deserialize, Default, Clone)]
pub struct SessionEntry {
    #[serde(default)]
    pub session_id: String,
    #[serde(default)]
    pub capability: String,
    #[serde(default)]
    pub share_name: String,
    #[serde(default)]
    pub file_count: u64,
    #[serde(default)]
    pub total_bytes: u64,
}

// `backend list --json`
#[derive(Deserialize, Default, Clone)]
pub struct BackendEntry {
    #[serde(default)]
    pub id: String,
    #[serde(default)]
    pub capability: String,
    #[serde(default)]
    pub support_tier: String,
    /// Kept from the CLI payload for a future detail view; the summary table
    /// shows the blocking reason instead of the prose description.
    #[allow(dead_code)]
    #[serde(default)]
    pub summary: String,
    #[serde(default)]
    pub ready: bool,
    #[serde(default)]
    pub missing: Vec<String>,
}

impl BackendEntry {
    /// Why a backend cannot be used, in the CLI's own vocabulary.
    pub fn blocked_by(&self) -> String {
        if self.missing.is_empty() {
            "ready".into()
        } else {
            self.missing.join(", ").replace('_', " ")
        }
    }
}

impl PeerEntry {
    pub fn label(&self) -> String {
        if self.device_label.is_empty() {
            self.device_id.clone()
        } else {
            self.device_label.clone()
        }
    }
}

impl SessionEntry {
    pub fn summary(&self) -> String {
        let name = if self.share_name.is_empty() {
            "untitled share".to_string()
        } else {
            self.share_name.clone()
        };
        format!(
            "{} · {} file{} · {}",
            name,
            self.file_count,
            if self.file_count == 1 { "" } else { "s" },
            human_bytes(self.total_bytes)
        )
    }
}

/// Compact byte sizes for the TUI, e.g. `1.4 MB`.
pub fn human_bytes(bytes: u64) -> String {
    const UNITS: [&str; 5] = ["B", "KB", "MB", "GB", "TB"];
    let mut value = bytes as f64;
    let mut unit = 0;
    while value >= 1024.0 && unit + 1 < UNITS.len() {
        value /= 1024.0;
        unit += 1;
    }
    if unit == 0 {
        format!("{} {}", bytes, UNITS[unit])
    } else {
        format!("{:.1} {}", value, UNITS[unit])
    }
}

#[derive(Default, Clone)]
pub struct Model {
    pub snapshot: Snapshot,
    pub health: Health,
    pub device: PeerSelf,
    pub status: Status,
    pub inbox: Inbox,
    pub history: Vec<HistoryEntry>,
    pub clipboard_history: Vec<ClipboardItem>,
    pub peers: Vec<PeerEntry>,
    pub sessions: Vec<SessionEntry>,
    pub backends: Vec<BackendEntry>,
    pub load_errors: Vec<String>,
}

impl Model {
    pub fn device_label(&self) -> String {
        if self.device.device_label.is_empty() {
            "this device".into()
        } else {
            self.device.device_label.clone()
        }
    }

    pub fn version(&self) -> String {
        if self.status.version.is_empty() {
            "unknown".into()
        } else {
            self.status.version.clone()
        }
    }

    pub fn state_dir(&self) -> String {
        self.status.paths.state_dir.clone()
    }

    /// Loads the whole model. Each field degrades gracefully on error, so a
    /// failing command (or a missing CLI on PATH) is reported in place rather
    /// than killing the interface.
    pub fn load(run: &dyn Fn(&[&str]) -> Result<String, String>) -> Result<Model, String> {
        let mut model = Model::default();

        // Record each failed CLI call so the UI can say why a panel is empty
        // instead of silently showing zeros that look like real data.
        let mut load_errors: Vec<String> = Vec::new();
        let mut parse_into =
            |args: &[&str], label: &str, sink: &mut dyn FnMut(Option<String>)| match run(args) {
                Ok(text) if !text.trim().is_empty() => sink(Some(text)),
                Ok(_) => {
                    load_errors.push(format!("{label}: empty output"));
                    sink(None);
                }
                Err(err) => {
                    load_errors.push(format!("{label}: {err}"));
                    sink(None);
                }
            };

        parse_into(&["status", "--json"], "status", &mut |v| {
            if let Some(v) = v {
                model.status = serde_json::from_str(&v).unwrap_or_default();
            }
        });
        parse_into(&["monitor", "snapshot", "--json"], "monitor", &mut |v| {
            if let Some(v) = v {
                model.snapshot = serde_json::from_str(&v).unwrap_or_default();
            }
        });
        parse_into(&["doctor", "--json"], "doctor", &mut |v| {
            if let Some(v) = v {
                let doctor: DoctorJson = serde_json::from_str(&v).unwrap_or_default();
                model.health = Health::from_doctor(doctor);
            }
        });
        parse_into(&["peer", "self", "--json"], "peer self", &mut |v| {
            if let Some(v) = v {
                model.device = serde_json::from_str(&v).unwrap_or_default();
            }
        });
        parse_into(&["inbox", "status", "--json"], "inbox status", &mut |v| {
            if let Some(v) = v {
                model.inbox = serde_json::from_str(&v).unwrap_or_default();
            }
        });
        parse_into(&["inbox", "history", "--json"], "inbox history", &mut |v| {
            if let Some(v) = v {
                model.history = serde_json::from_str(&v).unwrap_or_default();
            }
        });
        parse_into(&["peer", "list", "--json"], "peer list", &mut |v| {
            if let Some(v) = v {
                model.peers = serde_json::from_str(&v).unwrap_or_default();
            }
        });
        parse_into(&["session", "list", "--json"], "session list", &mut |v| {
            if let Some(v) = v {
                model.sessions = serde_json::from_str(&v).unwrap_or_default();
            }
        });
        parse_into(&["backend", "list", "--json"], "backend list", &mut |v| {
            if let Some(v) = v {
                model.backends = serde_json::from_str(&v).unwrap_or_default();
            }
        });
        parse_into(
            &["clipboard-sync", "history", "--json"],
            "clipboard history",
            &mut |v| {
                if let Some(v) = v {
                    model.clipboard_history = serde_json::from_str(&v).unwrap_or_default();
                }
            },
        );

        model.load_errors = load_errors;
        Ok(model)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn byte_sizes_stay_readable() {
        assert_eq!(human_bytes(0), "0 B");
        assert_eq!(human_bytes(512), "512 B");
        assert_eq!(human_bytes(1024), "1.0 KB");
        assert_eq!(human_bytes(1536), "1.5 KB");
        assert_eq!(human_bytes(5 * 1024 * 1024), "5.0 MB");
    }

    #[test]
    fn session_summary_pluralises_file_count() {
        let one = SessionEntry {
            share_name: "notes.txt".into(),
            file_count: 1,
            total_bytes: 16,
            ..Default::default()
        };
        assert_eq!(one.summary(), "notes.txt · 1 file · 16 B");

        let many = SessionEntry {
            share_name: "photos".into(),
            file_count: 12,
            total_bytes: 2048,
            ..Default::default()
        };
        assert_eq!(many.summary(), "photos · 12 files · 2.0 KB");

        let unnamed = SessionEntry::default();
        assert_eq!(unnamed.summary(), "untitled share · 0 files · 0 B");
    }

    #[test]
    fn blocked_backend_reads_as_prose() {
        let ready = BackendEntry {
            ready: true,
            ..Default::default()
        };
        assert_eq!(ready.blocked_by(), "ready");

        // Missing capabilities come from the CLI with underscores; the TUI shows
        // them as words so the reason is readable at a glance.
        let blocked = BackendEntry {
            missing: vec!["wayland_session".into(), "pipewire_active".into()],
            ..Default::default()
        };
        assert_eq!(blocked.blocked_by(), "wayland session, pipewire active");
    }

    #[test]
    fn peer_label_falls_back_to_the_device_id() {
        let unnamed = PeerEntry {
            device_id: "phone-test-01".into(),
            ..Default::default()
        };
        assert_eq!(unnamed.label(), "phone-test-01");

        let named = PeerEntry {
            device_id: "phone-test-01".into(),
            device_label: "Test Phone".into(),
            ..Default::default()
        };
        assert_eq!(named.label(), "Test Phone");
    }
}
