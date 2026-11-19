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

        model.status = parse(run, &["status", "--json"]).unwrap_or_default();
        model.snapshot = parse(run, &["monitor", "snapshot", "--json"]).unwrap_or_default();
        let doctor: DoctorJson = parse(run, &["doctor", "--json"]).unwrap_or_default();
        model.health = Health::from_doctor(doctor);
        model.device = parse(run, &["peer", "self", "--json"]).unwrap_or_default();
        model.inbox = parse(run, &["inbox", "status", "--json"]).unwrap_or_default();
        model.history = parse(run, &["inbox", "history", "--json"]).unwrap_or_default();
        model.clipboard_history =
            parse(run, &["clipboard-sync", "history", "--json"]).unwrap_or_default();

        Ok(model)
    }
}

fn parse<T: serde::de::DeserializeOwned>(
    run: &dyn Fn(&[&str]) -> Result<String, String>,
    args: &[&str],
) -> Option<T> {
    let text = run(args).ok()?;
    if text.is_empty() {
        return None;
    }
    serde_json::from_str(&text).ok()
}
