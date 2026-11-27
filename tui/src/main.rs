// hyprshare-tui: a terminal front-end for hyprshare.
//
// Architecture: identical to the desktop GUI — every piece of data and every
// action is a subprocess call to the installed `hyprshare` CLI (using its
// --json output), so the TUI and the command line can never disagree.

use std::process::Command;
use std::sync::mpsc;
use std::thread;
use std::time::{Duration, Instant};

use ratatui::crossterm::event::{self, Event, KeyCode, KeyEventKind};
use ratatui::layout::{Alignment, Constraint, Layout, Rect};
use ratatui::style::{Color, Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{
    Block, BorderType, Borders, Cell, Clear, Padding, Paragraph, Row, Table, Tabs, Wrap,
};

mod data;
use data::Model;

// ---------------------------------------------------------------------------
// Palette. A single place so every panel agrees on what "muted" or "ok" means.
// ---------------------------------------------------------------------------
const ACCENT: Color = Color::Rgb(137, 180, 250);
const OK: Color = Color::Rgb(166, 227, 161);
const WARN: Color = Color::Rgb(249, 226, 175);
const ERR: Color = Color::Rgb(243, 139, 168);
const MAUVE: Color = Color::Rgb(203, 166, 247);
const TEXT: Color = Color::Rgb(205, 214, 244);
const MUTED: Color = Color::Rgb(127, 132, 156);
const SURFACE: Color = Color::Rgb(49, 50, 68);

fn cli_json(args: &[&str]) -> Result<String, String> {
    let output = Command::new("hyprshare")
        .args(args)
        .output()
        .map_err(|e| format!("failed to run hyprshare: {e}"))?;
    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr).trim().to_string();
        return Err(if stderr.is_empty() {
            format!("hyprshare {} failed", args.join(" "))
        } else {
            stderr
        });
    }
    Ok(String::from_utf8_lossy(&output.stdout).trim().to_string())
}

enum Message {
    Loaded(Model),
}

/// Start `inbox serve` in the background with a fresh token. The server
/// blocks waiting for a sender, so it must be spawned detached.
fn spawn_inbox_serve() -> Result<String, String> {
    use std::time::{SystemTime, UNIX_EPOCH};
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|e| e.to_string())?
        .as_nanos();
    let token = format!("{:016x}{:08x}", nanos, std::process::id());
    let port = 30000 + (nanos % 20000) as u16;

    // The CLI default receive dir may not exist; pick a known-writable one
    // so the background server doesn't fail on startup. No extra deps needed.
    let to = std::env::var_os("HOME")
        .map(std::path::PathBuf::from)
        .unwrap_or_else(std::env::temp_dir)
        .join("Downloads")
        .join("hyprshare-tui");
    std::fs::create_dir_all(&to).map_err(|e| format!("failed to create receive dir: {e}"))?;

    Command::new("hyprshare")
        .args([
            "inbox",
            "serve",
            "--port",
            &port.to_string(),
            "--token",
            &token,
            "--to",
            &to.to_string_lossy(),
        ])
        .stdin(std::process::Stdio::null())
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()
        .map_err(|e| format!("failed to start inbox serve: {e}"))?;
    Ok(format!(
        "Serving on :{port} · token {token} · LAN peers: use this machine's IP:{port}"
    ))
}

fn spawn_load(sender: mpsc::Sender<Message>) {
    thread::spawn(move || {
        let model = Model::load(&cli_json).unwrap_or_default();
        let _ = sender.send(Message::Loaded(model));
    });
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut terminal = ratatui::init();

    let (sender, receiver) = mpsc::channel::<Message>();
    spawn_load(sender.clone());

    let mut app = App::new();
    let mut last_refresh = Instant::now();

    loop {
        // Draw on every iteration only when something changed; poll events in
        // small steps so input stays responsive while data loads.
        terminal.draw(|frame| app.render(frame))?;

        if event::poll(Duration::from_millis(100))? {
            if let Event::Key(key) = event::read()? {
                if key.kind == KeyEventKind::Press {
                    // Confirmation mode takes priority over all other keys.
                    if let Some(pending) = app.confirm.clone() {
                        match key.code {
                            KeyCode::Char('y') | KeyCode::Char('Y') => {
                                app.confirm = None;
                                let args = pending.command();
                                app.notice =
                                    Some(match Command::new("hyprshare").args(&args).output() {
                                        Ok(out) if out.status.success() => pending.done_message(),
                                        Ok(out) => format!(
                                            "failed: {}",
                                            String::from_utf8_lossy(&out.stderr).trim()
                                        ),
                                        Err(e) => format!("failed: {e}"),
                                    });
                                app.refreshing = true;
                                spawn_load(sender.clone());
                                last_refresh = Instant::now();
                            }
                            KeyCode::Char('n') | KeyCode::Char('N') | KeyCode::Esc => {
                                app.confirm = None;
                            }
                            _ => {}
                        }
                        continue;
                    }
                    match key.code {
                        KeyCode::Char('q') => break,
                        KeyCode::Tab | KeyCode::Right => app.next_tab(),
                        KeyCode::BackTab | KeyCode::Left => app.prev_tab(),
                        KeyCode::Char('1') => app.tab = 0,
                        KeyCode::Char('2') => app.tab = 1,
                        KeyCode::Char('3') => app.tab = 2,
                        KeyCode::Char('4') => app.tab = 3,
                        KeyCode::Char('5') => app.tab = 4,
                        KeyCode::Char('6') => app.tab = 5,
                        KeyCode::Char('r') if !app.write_mode => {
                            // Keep the previous model on screen so a refresh
                            // never blanks the interface.
                            app.refreshing = true;
                            spawn_load(sender.clone());
                            last_refresh = Instant::now();
                        }
                        KeyCode::Char('w') if !app.write_mode => app.write_mode = true,
                        KeyCode::Esc if app.write_mode => app.write_mode = false,
                        KeyCode::Char('s') if !app.write_mode && app.tab == 1 => {
                            app.serve_status = Some(
                                spawn_inbox_serve()
                                    .unwrap_or_else(|e| format!("failed to serve: {e}")),
                            );
                        }
                        KeyCode::Char('d') if !app.write_mode && app.tab == 1 => {
                            app.confirm = Some(PendingAction::ClearHistory);
                        }
                        // Destructive actions on the list pages work on the
                        // highlighted row and always ask before running.
                        KeyCode::Char('d') if !app.write_mode && app.tab == 4 => {
                            if let Some(session) = app
                                .model
                                .as_ref()
                                .and_then(|m| m.sessions.get(app.sess_sel))
                            {
                                app.confirm =
                                    Some(PendingAction::DeleteSession(session.session_id.clone()));
                            }
                        }
                        KeyCode::Char('d') if !app.write_mode && app.tab == 3 => {
                            if let Some(peer) =
                                app.model.as_ref().and_then(|m| m.peers.get(app.dev_sel))
                            {
                                app.confirm =
                                    Some(PendingAction::RemovePeer(peer.device_id.clone()));
                            }
                        }
                        // List navigation: clipboard, devices and sessions.
                        KeyCode::Down | KeyCode::Char('j')
                            if !app.write_mode && matches!(app.tab, 2 | 3 | 4) =>
                        {
                            app.select_next();
                        }
                        KeyCode::Up | KeyCode::Char('k')
                            if !app.write_mode && matches!(app.tab, 2 | 3 | 4) =>
                        {
                            app.select_prev();
                        }
                        KeyCode::Enter if !app.write_mode && app.tab == 2 => {
                            app.notice = Some(app.copy_selected());
                        }
                        KeyCode::Enter if app.write_mode => {
                            let text = app.write_input.clone();
                            if !text.trim().is_empty() {
                                app.write_status = match Command::new("hyprshare")
                                    .args(["clipboard", "write", "--text", &text])
                                    .output()
                                {
                                    Ok(out) if out.status.success() => {
                                        app.write_input.clear();
                                        "written to the clipboard".to_string()
                                    }
                                    Ok(out) => format!(
                                        "failed: {}",
                                        String::from_utf8_lossy(&out.stderr).trim()
                                    ),
                                    Err(e) => format!("failed: {e}"),
                                };
                            }
                        }
                        KeyCode::Char(c) if app.write_mode => {
                            app.write_input.push(c);
                            app.write_status.clear();
                        }
                        KeyCode::Backspace if app.write_mode => {
                            app.write_input.pop();
                            app.write_status.clear();
                        }
                        _ => {}
                    }
                }
            }
        }

        // Background refresh every 10 s (skipped while the user types).
        if !app.write_mode
            && app.model.is_some()
            && last_refresh.elapsed() > Duration::from_secs(10)
        {
            app.refreshing = true;
            spawn_load(sender.clone());
            last_refresh = Instant::now();
        }

        if let Ok(Message::Loaded(model)) = receiver.try_recv() {
            app.clip_sel = 0;
            app.model = Some(model);
            app.refreshing = false;
        }
    }

    ratatui::restore();
    Ok(())
}

/// A destructive action waiting for the user to press `y`.
#[derive(Clone)]
enum PendingAction {
    ClearHistory,
    DeleteSession(String),
    RemovePeer(String),
}

impl PendingAction {
    fn prompt(&self) -> String {
        match self {
            Self::ClearHistory => "Clear all transfer history?".to_string(),
            Self::DeleteSession(id) => format!("Delete staged session {id}?"),
            Self::RemovePeer(id) => format!("Stop trusting {id}?"),
        }
    }

    fn command(&self) -> Vec<String> {
        match self {
            Self::ClearHistory => ["inbox", "history", "clear", "--json"]
                .iter()
                .map(|s| s.to_string())
                .collect(),
            Self::DeleteSession(id) => ["session", "delete", id, "--json"]
                .iter()
                .map(|s| s.to_string())
                .collect(),
            Self::RemovePeer(id) => ["peer", "remove", id, "--json"]
                .iter()
                .map(|s| s.to_string())
                .collect(),
        }
    }

    fn done_message(&self) -> String {
        match self {
            Self::ClearHistory => "Transfer history cleared".to_string(),
            Self::DeleteSession(id) => format!("deleted session {id}"),
            Self::RemovePeer(id) => format!("stopped trusting {id}"),
        }
    }
}

struct App {
    tab: usize,
    model: Option<Model>,
    refreshing: bool,
    write_mode: bool,
    write_input: String,
    write_status: String,
    serve_status: Option<String>,
    confirm: Option<PendingAction>,
    notice: Option<String>,
    clip_sel: usize,
    dev_sel: usize,
    sess_sel: usize,
}

const TABS: [&str; 6] = [
    "Dashboard",
    "Inbox",
    "Clipboard",
    "Devices",
    "Sessions",
    "Capture",
];

impl App {
    fn new() -> Self {
        Self {
            tab: 0,
            model: None,
            refreshing: true,
            write_mode: false,
            write_input: String::new(),
            write_status: String::new(),
            serve_status: None,
            confirm: None,
            notice: None,
            clip_sel: 0,
            dev_sel: 0,
            sess_sel: 0,
        }
    }

    /// Length of the list the arrow keys move through on the current tab.
    fn selectable_len(&self) -> usize {
        let Some(model) = self.model.as_ref() else {
            return 0;
        };
        match self.tab {
            2 => model.clipboard_history.len(),
            3 => model.peers.len(),
            4 => model.sessions.len(),
            _ => 0,
        }
    }

    fn select_next(&mut self) {
        let len = self.selectable_len();
        if len == 0 {
            return;
        }
        let slot = match self.tab {
            3 => &mut self.dev_sel,
            4 => &mut self.sess_sel,
            _ => &mut self.clip_sel,
        };
        *slot = (*slot + 1).min(len - 1);
    }

    fn select_prev(&mut self) {
        let slot = match self.tab {
            3 => &mut self.dev_sel,
            4 => &mut self.sess_sel,
            _ => &mut self.clip_sel,
        };
        *slot = slot.saturating_sub(1);
    }

    fn next_tab(&mut self) {
        self.tab = (self.tab + 1) % TABS.len();
        self.notice = None;
    }

    fn prev_tab(&mut self) {
        self.tab = (self.tab + TABS.len() - 1) % TABS.len();
        self.notice = None;
    }

    /// Writes the highlighted clipboard-history entry back to the clipboard.
    fn copy_selected(&self) -> String {
        let Some(model) = self.model.as_ref() else {
            return "nothing to copy".to_string();
        };
        let Some(item) = model.clipboard_history.get(self.clip_sel) else {
            return "nothing to copy".to_string();
        };
        match Command::new("hyprshare")
            .args(["clipboard", "write", "--text", &item.content])
            .output()
        {
            Ok(out) if out.status.success() => format!("copied entry #{}", self.clip_sel + 1),
            Ok(out) => format!("failed: {}", String::from_utf8_lossy(&out.stderr).trim()),
            Err(e) => format!("failed: {e}"),
        }
    }

    fn render(&mut self, frame: &mut ratatui::Frame) {
        let [header_area, tabs_area, body_area, help_area] = {
            let chunks = Layout::vertical([
                Constraint::Length(1),
                Constraint::Length(3),
                Constraint::Min(3),
                Constraint::Length(1),
            ])
            .split(frame.area());
            [chunks[0], chunks[1], chunks[2], chunks[3]]
        };

        self.render_header(frame, header_area);
        self.render_tabs(frame, tabs_area);

        match self.model.as_ref() {
            Some(model) => match self.tab {
                0 => Self::render_dashboard(frame, model, body_area),
                1 => Self::render_inbox(frame, model, body_area),
                2 => Self::render_clipboard(frame, model, body_area, self.clip_sel),
                3 => Self::render_devices(frame, model, body_area, self.dev_sel),
                4 => Self::render_sessions(frame, model, body_area, self.sess_sel),
                _ => Self::render_capture(frame, model, body_area),
            },
            None => {
                let text = model_error_hint(self).unwrap_or_else(|| "loading …".to_string());
                frame.render_widget(
                    Paragraph::new(text)
                        .alignment(Alignment::Center)
                        .block(panel("Loading")),
                    body_area,
                );
            }
        }

        if self.write_mode {
            self.render_write_popup(frame, body_area);
        }

        self.render_footer(frame, help_area);
    }

    fn render_header(&self, frame: &mut ratatui::Frame, area: Rect) {
        let [brand_area, info_area, right_area] = {
            let chunks = Layout::horizontal([
                Constraint::Length(12),
                Constraint::Min(10),
                Constraint::Length(18),
            ])
            .split(area);
            [chunks[0], chunks[1], chunks[2]]
        };

        frame.render_widget(
            Paragraph::new(Span::styled(
                " hyprshare ",
                Style::new()
                    .fg(Color::Rgb(30, 30, 46))
                    .bg(ACCENT)
                    .add_modifier(Modifier::BOLD),
            )),
            brand_area,
        );

        let (version, device) = match self.model.as_ref() {
            Some(model) => (model.version(), model.device_label()),
            None => ("—".to_string(), "this device".to_string()),
        };
        frame.render_widget(
            Paragraph::new(Line::from(vec![
                Span::styled(format!(" {version}"), Style::new().fg(MUTED)),
                Span::styled("  ·  ", Style::new().fg(SURFACE)),
                Span::styled(device, Style::new().fg(TEXT)),
            ])),
            info_area,
        );

        let right = if self.refreshing {
            Span::styled("refreshing ⟳ ", Style::new().fg(WARN))
        } else {
            Span::styled("ready ● ", Style::new().fg(OK))
        };
        frame.render_widget(
            Paragraph::new(Line::from(right)).alignment(Alignment::Right),
            right_area,
        );
    }

    fn render_tabs(&self, frame: &mut ratatui::Frame, area: Rect) {
        let counts = self
            .model
            .as_ref()
            .map(|m| (m.history.len(), m.clipboard_history.len()));
        let titles: Vec<Line> = TABS
            .iter()
            .enumerate()
            .map(|(index, name)| {
                let count = counts.as_ref().and_then(|(inbox, clipboard)| match index {
                    1 => Some(*inbox),
                    2 => Some(*clipboard),
                    _ => None,
                });
                let label = match count {
                    Some(n) => format!(" {} {} · {n} ", index + 1, name),
                    None => format!(" {} {name} ", index + 1),
                };
                Line::from(label)
            })
            .collect();

        let tabs = Tabs::new(titles)
            .block(
                Block::new()
                    .borders(Borders::ALL)
                    .border_type(BorderType::Rounded)
                    .border_style(Style::new().fg(SURFACE))
                    .padding(Padding::horizontal(1))
                    .title(Span::styled(" Navigate ", Style::new().fg(MUTED))),
            )
            .select(self.tab)
            .divider(Span::styled("│", Style::new().fg(SURFACE)))
            .highlight_style(
                Style::new()
                    .fg(ACCENT)
                    .add_modifier(Modifier::BOLD | Modifier::UNDERLINED),
            );
        frame.render_widget(tabs, area);
    }

    fn render_footer(&self, frame: &mut ratatui::Frame, area: Rect) {
        let message = if let Some(pending) = &self.confirm {
            Some((pending.prompt(), ERR))
        } else if self.write_mode {
            None
        } else if let Some(error) = model_error_hint(self) {
            Some((error, ERR))
        } else if let Some(notice) = &self.notice {
            Some((notice.clone(), OK))
        } else if let Some(serve) = &self.serve_status {
            Some((serve.clone(), MAUVE))
        } else {
            None
        };

        // Only reserve space on the right when there is something to show, so
        // the key hints are never truncated for an empty notice area.
        let notice_width = message
            .as_ref()
            .map(|(text, _)| (text.chars().count() as u16 + 2).min(area.width / 2))
            .unwrap_or(0);
        let footer_chunks =
            Layout::horizontal([Constraint::Min(20), Constraint::Length(notice_width)]).split(area);
        let keys_area = footer_chunks[0];
        let notice_area = footer_chunks[1];

        let keys: Vec<(&str, &str)> = if self.write_mode {
            vec![("Enter", "write"), ("Esc", "cancel")]
        } else if self.confirm.is_some() {
            vec![("y", "confirm"), ("n/Esc", "cancel")]
        } else if self.tab == 4 {
            vec![
                ("↑↓", "select"),
                ("d", "delete"),
                ("Tab", "page"),
                ("r", "refresh"),
                ("q", "quit"),
            ]
        } else if self.tab == 3 {
            vec![
                ("↑↓", "select"),
                ("d", "untrust"),
                ("Tab", "page"),
                ("r", "refresh"),
                ("q", "quit"),
            ]
        } else if self.tab == 5 {
            vec![("Tab", "page"), ("r", "refresh"), ("q", "quit")]
        } else if self.tab == 2 {
            vec![
                ("↑↓", "select"),
                ("Enter", "copy"),
                ("Tab", "page"),
                ("w", "write"),
                ("r", "refresh"),
                ("q", "quit"),
            ]
        } else if self.tab == 1 {
            vec![
                ("s", "serve"),
                ("d", "clear history"),
                ("Tab", "page"),
                ("w", "write"),
                ("r", "refresh"),
                ("q", "quit"),
            ]
        } else {
            vec![
                ("Tab", "page"),
                ("r", "refresh"),
                ("w", "write"),
                ("q", "quit"),
            ]
        };

        let mut spans: Vec<Span> = Vec::new();
        for (index, (key, action)) in keys.iter().enumerate() {
            if index > 0 {
                spans.push(Span::styled("  ", Style::new()));
            }
            spans.push(Span::styled(
                format!(" {key} "),
                Style::new().fg(ACCENT).bg(SURFACE),
            ));
            spans.push(Span::styled(format!(" {action}"), Style::new().fg(MUTED)));
        }
        frame.render_widget(Paragraph::new(Line::from(spans)), keys_area);

        if let Some((text, color)) = message {
            frame.render_widget(
                Paragraph::new(Span::styled(text, Style::new().fg(color)))
                    .alignment(Alignment::Right),
                notice_area,
            );
        }
    }

    fn render_write_popup(&self, frame: &mut ratatui::Frame, body: Rect) {
        let width = body.width.saturating_sub(8).min(72);
        let area = Rect {
            x: body.x + (body.width.saturating_sub(width)) / 2,
            y: body.y + body.height.saturating_sub(6) / 2,
            width,
            height: 5,
        };
        frame.render_widget(Clear, area);
        let hint = if self.write_status.is_empty() {
            "Enter writes · Esc cancels".to_string()
        } else {
            self.write_status.clone()
        };
        let popup = Paragraph::new(vec![
            Line::from(Span::styled(
                format!(" {}▏", self.write_input),
                Style::new().fg(TEXT),
            )),
            Line::from(""),
            Line::from(Span::styled(hint, Style::new().fg(MUTED))),
        ])
        .block(
            Block::new()
                .borders(Borders::ALL)
                .border_type(BorderType::Rounded)
                .border_style(Style::new().fg(ACCENT))
                .padding(Padding::horizontal(1))
                .title(Span::styled(
                    " Write to clipboard ",
                    Style::new().fg(ACCENT),
                )),
        );
        frame.render_widget(popup, area);
    }

    fn render_dashboard(frame: &mut ratatui::Frame, model: &Model, area: Rect) {
        let metrics_chunks =
            Layout::vertical([Constraint::Length(3), Constraint::Min(3)]).split(area);
        let metrics_area = metrics_chunks[0];
        let panels_area = metrics_chunks[1];

        // Metric strip: three gauges plus the health tally.
        let cells = Layout::horizontal([
            Constraint::Percentage(25),
            Constraint::Percentage(25),
            Constraint::Percentage(25),
            Constraint::Percentage(25),
        ])
        .split(metrics_area);

        for (index, (label, value)) in [
            ("CPU", model.snapshot.cpu),
            ("MEMORY", model.snapshot.memory),
            ("DISK", model.snapshot.disk),
        ]
        .iter()
        .enumerate()
        {
            frame.render_widget(metric_line(label, *value), cells[index]);
        }

        let health = Paragraph::new(Line::from(vec![
            Span::styled(
                format!("{} ", model.health.pass),
                Style::new().fg(OK).add_modifier(Modifier::BOLD),
            ),
            Span::styled("pass", Style::new().fg(MUTED)),
            Span::styled(" · ", Style::new().fg(SURFACE)),
            Span::styled(
                format!("{} ", model.health.fail),
                Style::new().fg(ERR).add_modifier(Modifier::BOLD),
            ),
            Span::styled("fail", Style::new().fg(MUTED)),
            Span::styled(" · ", Style::new().fg(SURFACE)),
            Span::styled(
                format!("{} ", model.health.warn),
                Style::new().fg(WARN).add_modifier(Modifier::BOLD),
            ),
            Span::styled("warn", Style::new().fg(MUTED)),
        ]))
        .block(metric_block("HEALTH"));
        frame.render_widget(health, cells[3]);

        let [device_area, health_area] = {
            let chunks = Layout::horizontal([Constraint::Percentage(40), Constraint::Min(30)])
                .split(panels_area);
            [chunks[0], chunks[1]]
        };

        // This device.
        let device_rows = vec![
            kv_row("device", model.device_label()),
            kv_row("version", model.version()),
            kv_row("state", model.state_dir()),
            kv_row("receive", &model.inbox.resolved_receive_dir),
            kv_row(
                "listen",
                &format!("{}:{}", model.inbox.default_host, model.inbox.default_port),
            ),
        ];
        let device = Table::new(device_rows, [Constraint::Length(9), Constraint::Min(8)])
            .block(panel("This device"))
            .column_spacing(1);
        frame.render_widget(device, device_area);

        // Health issues, failures first.
        let mut issues: Vec<_> = model
            .health
            .results
            .iter()
            .filter(|check| check.status != "pass")
            .collect();
        issues.sort_by_key(|check| if check.status == "fail" { 0 } else { 1 });

        if issues.is_empty() {
            frame.render_widget(
                Paragraph::new(vec![
                    Line::from(""),
                    Line::from(Span::styled(
                        format!("✓  all {} checks pass", model.health.checks),
                        Style::new().fg(OK).add_modifier(Modifier::BOLD),
                    )),
                    Line::from(Span::styled(
                        "nothing needs attention on this machine",
                        Style::new().fg(MUTED),
                    )),
                ])
                .alignment(Alignment::Center)
                .block(panel("Health")),
                health_area,
            );
            return;
        }

        let rows: Vec<Row> = issues
            .iter()
            .map(|check| {
                let (symbol, color) = match check.status.as_str() {
                    "fail" => ("✖ fail", ERR),
                    "warn" => ("▲ warn", WARN),
                    _ => ("· info", MUTED),
                };
                Row::new(vec![
                    Cell::from(symbol).style(Style::new().fg(color).add_modifier(Modifier::BOLD)),
                    Cell::from(check.id.clone()).style(Style::new().fg(TEXT)),
                    Cell::from(check.summary.clone()).style(Style::new().fg(MUTED)),
                ])
            })
            .collect();

        let table = Table::new(
            rows,
            [
                Constraint::Length(8),
                Constraint::Length(24),
                Constraint::Min(16),
            ],
        )
        .header(
            Row::new(vec!["", "CHECK", "DETAIL"])
                .style(Style::new().fg(MUTED).add_modifier(Modifier::BOLD)),
        )
        .block(panel(&format!("Health · {} need attention", issues.len())))
        .column_spacing(1);
        frame.render_widget(table, health_area);
    }

    fn render_inbox(frame: &mut ratatui::Frame, model: &Model, area: Rect) {
        let [info_area, history_area] = {
            let chunks = Layout::vertical([Constraint::Length(4), Constraint::Min(3)]).split(area);
            [chunks[0], chunks[1]]
        };

        let writable = if model.inbox.writable {
            Span::styled("writable", Style::new().fg(OK))
        } else {
            Span::styled("not writable", Style::new().fg(ERR))
        };
        let info = Paragraph::new(vec![
            Line::from(vec![
                Span::styled("receive dir  ", Style::new().fg(MUTED)),
                Span::styled(
                    model.inbox.resolved_receive_dir.clone(),
                    Style::new().fg(TEXT),
                ),
                Span::raw("  "),
                writable,
            ]),
            Line::from(vec![
                Span::styled("listening on ", Style::new().fg(MUTED)),
                Span::styled(
                    format!("{}:{}", model.inbox.default_host, model.inbox.default_port),
                    Style::new().fg(ACCENT),
                ),
                Span::styled("   press ", Style::new().fg(MUTED)),
                Span::styled(" s ", Style::new().fg(ACCENT).bg(SURFACE)),
                Span::styled(" to start receiving", Style::new().fg(MUTED)),
            ]),
        ])
        .block(panel("Inbox"))
        .wrap(Wrap { trim: true });
        frame.render_widget(info, info_area);

        if model.history.is_empty() {
            frame.render_widget(
                Paragraph::new(vec![
                    Line::from(""),
                    Line::from(Span::styled(
                        "No transfers yet",
                        Style::new().fg(TEXT).add_modifier(Modifier::BOLD),
                    )),
                    Line::from(Span::styled(
                        "press s to serve an inbox and send a file to this machine",
                        Style::new().fg(MUTED),
                    )),
                ])
                .alignment(Alignment::Center)
                .block(panel("Transfer history")),
                history_area,
            );
            return;
        }

        let rows: Vec<Row> = model
            .history
            .iter()
            .take(history_area.height.saturating_sub(3) as usize)
            .enumerate()
            .map(|(index, entry)| {
                let style = if index % 2 == 0 {
                    Style::new()
                } else {
                    Style::new().bg(Color::Rgb(36, 37, 54))
                };
                Row::new(vec![
                    Cell::from(entry.file_name.clone()).style(Style::new().fg(TEXT)),
                    Cell::from(format_bytes(entry.bytes_received)).style(Style::new().fg(MAUVE)),
                    Cell::from(time_ago(entry.received_at_unix)).style(Style::new().fg(MUTED)),
                ])
                .style(style)
            })
            .collect();

        let table = Table::new(
            rows,
            [
                Constraint::Min(20),
                Constraint::Length(10),
                Constraint::Length(10),
            ],
        )
        .header(
            Row::new(vec!["FILE", "SIZE", "RECEIVED"])
                .style(Style::new().fg(MUTED).add_modifier(Modifier::BOLD)),
        )
        .block(panel(&format!(
            "Transfer history · {}",
            model.history.len()
        )))
        .column_spacing(1);
        frame.render_widget(table, history_area);
    }

    fn render_clipboard(frame: &mut ratatui::Frame, model: &Model, area: Rect, selected: usize) {
        if model.clipboard_history.is_empty() {
            frame.render_widget(
                Paragraph::new(vec![
                    Line::from(""),
                    Line::from(Span::styled(
                        "Clipboard history is empty",
                        Style::new().fg(TEXT).add_modifier(Modifier::BOLD),
                    )),
                    Line::from(Span::styled(
                        "press w to write something, or start clipboard-sync",
                        Style::new().fg(MUTED),
                    )),
                ])
                .alignment(Alignment::Center)
                .block(panel("Clipboard history")),
                area,
            );
            return;
        }

        let rows: Vec<Row> = model
            .clipboard_history
            .iter()
            .take(area.height.saturating_sub(3) as usize)
            .enumerate()
            .map(|(index, item)| {
                let is_selected = index == selected;
                let base = if is_selected {
                    Style::new()
                        .bg(SURFACE)
                        .fg(TEXT)
                        .add_modifier(Modifier::BOLD)
                } else {
                    Style::new().fg(TEXT)
                };
                Row::new(vec![
                    Cell::from(format!("{:>2}", index + 1)).style(if is_selected {
                        base.fg(ACCENT)
                    } else {
                        Style::new().fg(MUTED)
                    }),
                    Cell::from(truncate_chars(&item.content, 96)).style(base),
                ])
            })
            .collect();

        let table = Table::new(rows, [Constraint::Length(3), Constraint::Min(20)])
            .header(
                Row::new(vec!["#", "CONTENT"])
                    .style(Style::new().fg(MUTED).add_modifier(Modifier::BOLD)),
            )
            .block(panel(&format!(
                "Clipboard history · {} · ↑↓ select, Enter copies",
                model.clipboard_history.len()
            )))
            .column_spacing(1);
        frame.render_widget(table, area);
    }

    /// Trusted peers plus this device's own identity.
    fn render_devices(frame: &mut ratatui::Frame, model: &Model, area: Rect, selected: usize) {
        if model.peers.is_empty() {
            frame.render_widget(
                Paragraph::new(vec![
                    Line::from(""),
                    Line::from(Span::styled(
                        "No trusted devices yet",
                        Style::new().fg(TEXT).add_modifier(Modifier::BOLD),
                    )),
                    Line::from(Span::styled(
                        format!("this device: {}", model.device_label()),
                        Style::new().fg(MUTED),
                    )),
                    Line::from(Span::styled(
                        "pair one with: hyprshare peer invite create",
                        Style::new().fg(MUTED),
                    )),
                ])
                .alignment(Alignment::Center)
                .block(panel("Devices")),
                area,
            );
            return;
        }

        let rows: Vec<Row> = model
            .peers
            .iter()
            .take(area.height.saturating_sub(3) as usize)
            .enumerate()
            .map(|(index, peer)| {
                let is_selected = index == selected;
                let base = if is_selected {
                    Style::new()
                        .bg(SURFACE)
                        .fg(TEXT)
                        .add_modifier(Modifier::BOLD)
                } else {
                    Style::new().fg(TEXT)
                };
                let paired = if peer.added_at_unix > 0 {
                    time_ago(peer.added_at_unix)
                } else {
                    "unknown".to_string()
                };
                Row::new(vec![
                    Cell::from(if is_selected { "›" } else { " " }).style(base.fg(ACCENT)),
                    Cell::from(truncate_chars(&peer.label(), 34)).style(base),
                    Cell::from(truncate_chars(&peer.device_id, 24)).style(Style::new().fg(MUTED)),
                    Cell::from(paired).style(Style::new().fg(MUTED)),
                ])
            })
            .collect();

        let table = Table::new(
            rows,
            [
                Constraint::Length(1),
                Constraint::Min(16),
                Constraint::Length(24),
                Constraint::Length(12),
            ],
        )
        .header(
            Row::new(vec!["", "DEVICE", "ID", "PAIRED"])
                .style(Style::new().fg(MUTED).add_modifier(Modifier::BOLD)),
        )
        .block(panel(&format!(
            "Devices · {} trusted · this device: {}",
            model.peers.len(),
            model.device_label()
        )))
        .column_spacing(1);
        frame.render_widget(table, area);
    }

    /// Staged share sessions.
    fn render_sessions(frame: &mut ratatui::Frame, model: &Model, area: Rect, selected: usize) {
        if model.sessions.is_empty() {
            frame.render_widget(
                Paragraph::new(vec![
                    Line::from(""),
                    Line::from(Span::styled(
                        "No staged sessions",
                        Style::new().fg(TEXT).add_modifier(Modifier::BOLD),
                    )),
                    Line::from(Span::styled(
                        "stage one with: hyprshare share file <path>",
                        Style::new().fg(MUTED),
                    )),
                ])
                .alignment(Alignment::Center)
                .block(panel("Sessions")),
                area,
            );
            return;
        }

        let rows: Vec<Row> = model
            .sessions
            .iter()
            .take(area.height.saturating_sub(3) as usize)
            .enumerate()
            .map(|(index, session)| {
                let is_selected = index == selected;
                let base = if is_selected {
                    Style::new()
                        .bg(SURFACE)
                        .fg(TEXT)
                        .add_modifier(Modifier::BOLD)
                } else {
                    Style::new().fg(TEXT)
                };
                Row::new(vec![
                    Cell::from(if is_selected { "›" } else { " " }).style(base.fg(ACCENT)),
                    Cell::from(truncate_chars(&session.summary(), 52)).style(base),
                    Cell::from(session.capability.clone()).style(Style::new().fg(MAUVE)),
                    Cell::from(truncate_chars(&session.session_id, 20))
                        .style(Style::new().fg(MUTED)),
                ])
            })
            .collect();

        let table = Table::new(
            rows,
            [
                Constraint::Length(1),
                Constraint::Min(24),
                Constraint::Length(12),
                Constraint::Length(20),
            ],
        )
        .header(
            Row::new(vec!["", "SHARE", "CAPABILITY", "SESSION"])
                .style(Style::new().fg(MUTED).add_modifier(Modifier::BOLD)),
        )
        .block(panel(&format!("Sessions · {}", model.sessions.len())))
        .column_spacing(1);
        frame.render_widget(table, area);
    }

    /// Capture backends and what each one is blocked on — the honest answer to
    /// "why can I not share my screen yet".
    fn render_capture(frame: &mut ratatui::Frame, model: &Model, area: Rect) {
        if model.backends.is_empty() {
            frame.render_widget(
                Paragraph::new(vec![
                    Line::from(""),
                    Line::from(Span::styled(
                        "No capture backends reported",
                        Style::new().fg(TEXT).add_modifier(Modifier::BOLD),
                    )),
                    Line::from(Span::styled(
                        "run: hyprshare backend list",
                        Style::new().fg(MUTED),
                    )),
                ])
                .alignment(Alignment::Center)
                .block(panel("Capture backends")),
                area,
            );
            return;
        }

        let rows: Vec<Row> = model
            .backends
            .iter()
            .take(area.height.saturating_sub(3) as usize)
            .map(|backend| {
                let (state, style) = if backend.ready {
                    ("ready", Style::new().fg(OK).add_modifier(Modifier::BOLD))
                } else {
                    ("blocked", Style::new().fg(ERR))
                };
                Row::new(vec![
                    Cell::from(state).style(style),
                    Cell::from(truncate_chars(
                        &format!("{} ({})", backend.id, backend.support_tier),
                        32,
                    ))
                    .style(Style::new().fg(TEXT)),
                    Cell::from(backend.capability.clone()).style(Style::new().fg(MAUVE)),
                    Cell::from(truncate_chars(&backend.blocked_by(), 40))
                        .style(Style::new().fg(MUTED)),
                ])
            })
            .collect();

        frame.render_widget(
            Table::new(
                rows,
                [
                    Constraint::Length(7),
                    Constraint::Length(34),
                    Constraint::Length(11),
                    Constraint::Min(20),
                ],
            )
            .header(
                Row::new(vec!["STATE", "BACKEND (TIER)", "CAPABILITY", "BLOCKED ON"])
                    .style(Style::new().fg(MUTED).add_modifier(Modifier::BOLD)),
            )
            .block(panel(&format!(
                "Capture backends · {} · {} ready",
                model.backends.len(),
                model.backends.iter().filter(|b| b.ready).count()
            )))
            .column_spacing(1),
            area,
        );
    }
}

/// Rounded, padded panel with a muted title — the shared frame for every pane.
fn panel(title: &str) -> Block<'static> {
    Block::new()
        .borders(Borders::ALL)
        .border_type(BorderType::Rounded)
        .border_style(Style::new().fg(SURFACE))
        .padding(Padding::horizontal(1))
        .title(Span::styled(
            format!(" {title} "),
            Style::new().fg(MUTED).add_modifier(Modifier::BOLD),
        ))
}

/// Compact panel used by the metric strip (no horizontal padding, tight title).
fn metric_block(title: &str) -> Block<'static> {
    Block::new()
        .borders(Borders::ALL)
        .border_type(BorderType::Rounded)
        .border_style(Style::new().fg(SURFACE))
        .title(Span::styled(
            format!(" {title} "),
            Style::new().fg(MUTED).add_modifier(Modifier::BOLD),
        ))
}

/// One-line percentage meter with a fixed-width bar, so the number never
/// shifts around as the fill changes (a ratatui Gauge centres its label over
/// the bar, which looks unstable in a 1-line cell).
fn metric_line(_label: &str, value: f64) -> Paragraph<'static> {
    const WIDTH: usize = 14;
    let ratio = (value / 100.0).clamp(0.0, 1.0);
    let filled = ((ratio * WIDTH as f64).round() as usize).min(WIDTH);
    let color = if value >= 85.0 {
        ERR
    } else if value >= 60.0 {
        WARN
    } else {
        OK
    };
    Paragraph::new(Line::from(vec![
        Span::styled("█".repeat(filled), Style::new().fg(color)),
        Span::styled("░".repeat(WIDTH - filled), Style::new().fg(SURFACE)),
        Span::styled(
            format!("  {value:.0}%"),
            Style::new().fg(TEXT).add_modifier(Modifier::BOLD),
        ),
    ]))
    .block(metric_block(_label))
}

fn model_error_hint(app: &App) -> Option<String> {
    app.model.as_ref().and_then(|model| {
        if model.load_errors.is_empty() {
            None
        } else {
            let shown = model
                .load_errors
                .iter()
                .take(2)
                .cloned()
                .collect::<Vec<_>>()
                .join(" · ");
            Some(format!("CLI data unavailable: {shown}"))
        }
    })
}

fn kv_row(key: &str, value: impl Into<String>) -> Row<'static> {
    Row::new(vec![
        Cell::from(key.to_string()).style(Style::new().fg(MUTED)),
        Cell::from(value.into()).style(Style::new().fg(TEXT)),
    ])
}

/// Truncates on character boundaries so multi-byte text (CJK) never panics.
fn truncate_chars(text: &str, limit: usize) -> String {
    let cleaned = text.replace(['\n', '\r'], " ");
    let chars: Vec<char> = cleaned.chars().collect();
    if chars.len() > limit {
        format!("{}…", chars[..limit].iter().collect::<String>())
    } else {
        cleaned
    }
}

fn format_bytes(bytes: u64) -> String {
    const KB: f64 = 1024.0;
    const MB: f64 = KB * 1024.0;
    const GB: f64 = MB * 1024.0;
    let value = bytes as f64;
    if value >= GB {
        format!("{:.1} GB", value / GB)
    } else if value >= MB {
        format!("{:.1} MB", value / MB)
    } else if value >= KB {
        format!("{:.1} KB", value / KB)
    } else {
        format!("{bytes} B")
    }
}

fn time_ago(unix: i64) -> String {
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs() as i64)
        .unwrap_or(0);
    let secs = (now - unix).max(0);
    if secs < 60 {
        format!("{secs}s ago")
    } else if secs < 3600 {
        format!("{}m ago", secs / 60)
    } else if secs < 86400 {
        format!("{}h ago", secs / 3600)
    } else {
        format!("{}d ago", secs / 86400)
    }
}
