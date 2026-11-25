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

#[derive(Default, Clone)]
pub struct Model {
    pub snapshot: Snapshot,
    pub health: Health,
    pub device: PeerSelf,
    pub status: Status,
    pub inbox: Inbox,
    pub history: Vec<HistoryEntry>,
    pub clipboard_history: Vec<ClipboardItem>,
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
