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
use ratatui::style::{Color, Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Borders, Cell, Clear, Paragraph, Row, Table, Tabs, Wrap};

mod data;
use data::Model;

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
                    if app.confirm_clear {
                        match key.code {
                            KeyCode::Char('y') | KeyCode::Char('Y') => {
                                app.confirm_clear = false;
                                app.notice = Some(match Command::new("hyprshare")
                                    .args(["inbox", "history", "clear", "--json"])
                                    .output()
                                {
                                    Ok(out) if out.status.success() => {
                                        "Transfer history cleared".to_string()
                                    }
                                    Ok(out) => format!(
                                        "failed: {}",
                                        String::from_utf8_lossy(&out.stderr).trim()
                                    ),
                                    Err(e) => format!("failed: {e}"),
                                });
                                app.refreshing = true;
                                app.model = None;
                                spawn_load(sender.clone());
                                last_refresh = Instant::now();
                            }
                            KeyCode::Char('n') | KeyCode::Char('N') | KeyCode::Esc => {
                                app.confirm_clear = false;
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
                        KeyCode::Char('r') => {
                            app.refreshing = true;
                            app.model = None;
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
                            app.confirm_clear = true;
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
            app.model = Some(model);
            app.refreshing = false;
        }
    }

    ratatui::restore();
    Ok(())
}

struct App {
    tab: usize,
    model: Option<Model>,
    refreshing: bool,
    write_mode: bool,
    write_input: String,
    write_status: String,
    serve_status: Option<String>,
    confirm_clear: bool,
    notice: Option<String>,
}

const TABS: [&str; 3] = ["Dashboard", "Inbox", "Clipboard"];

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
            confirm_clear: false,
            notice: None,
        }
    }

    fn next_tab(&mut self) {
        self.tab = (self.tab + 1) % TABS.len();
    }

    fn prev_tab(&mut self) {
        self.tab = (self.tab + TABS.len() - 1) % TABS.len();
    }

    fn render(&mut self, frame: &mut ratatui::Frame) {
        let [status_area, tabs_area, body_area, help_area] = {
            let chunks = ratatui::layout::Layout::vertical([
                ratatui::layout::Constraint::Length(1),
                ratatui::layout::Constraint::Length(3),
                ratatui::layout::Constraint::Min(1),
                ratatui::layout::Constraint::Length(1),
            ])
            .split(frame.area());
            [chunks[0], chunks[1], chunks[2], chunks[3]]
        };

        // Status line: what the TUI is doing right now.
        let status_text = if self.refreshing {
            "loading …".to_string()
        } else if self.confirm_clear {
            "Clear all transfer history? y = confirm · n / Esc = cancel".to_string()
        } else if self.write_mode {
            "WRITE MODE: type, Enter to write to the clipboard, Esc to cancel".to_string()
        } else if let Some(error) = model_error_hint(self) {
            error
        } else if let Some(notice) = &self.notice {
            notice.clone()
        } else if let Some(serve) = &self.serve_status {
            serve.clone()
        } else {
            format!("hyprshare TUI · {} ", TABS[self.tab])
        };
        let status = Paragraph::new(Line::from(vec![
            Span::styled(" hyprshare ", Style::new().bg(Color::Blue).fg(Color::White).add_modifier(Modifier::BOLD)),
            Span::raw(" "),
            Span::styled(status_text, Style::new().fg(Color::DarkGray)),
        ]));
        frame.render_widget(status, status_area);

        // Tabs.
        let tabs = Tabs::new(TABS.to_vec())
            .block(Block::new().borders(Borders::ALL).title("hyprshare"))
            .select(self.tab)
            .highlight_style(Style::new().fg(Color::Cyan).add_modifier(Modifier::BOLD));
        frame.render_widget(tabs, tabs_area);

        match self.model.as_ref() {
            Some(model) => match self.tab {
                0 => Self::render_dashboard(frame, model, body_area),
                1 => Self::render_inbox(frame, model, body_area),
                _ => Self::render_clipboard(frame, model, body_area),
            },
            None => {
                let text = if let Some(err) = model_error_hint(self) {
                    err
                } else {
                    "loading …".to_string()
                };
                frame.render_widget(
                    Paragraph::new(text)
                        .block(Block::new().borders(Borders::ALL).title("Loading"))
                        .wrap(Wrap { trim: true }),
                    body_area,
                );
            }
        }

        // Clipboard write input (drawn over the bottom of the body).
        if self.write_mode {
            let area = {
                let inner = ratatui::layout::Layout::vertical([
                    ratatui::layout::Constraint::Percentage(100),
                ])
                .split(body_area);
                ratatui::layout::Rect {
                    y: inner[0].bottom().saturating_sub(3),
                    ..inner[0]
                }
            };
            frame.render_widget(Clear, area);
            let popup = Paragraph::new(vec![
                Line::from(format!("> {}", self.write_input)),
                Line::from(Span::styled(
                    if self.write_status.is_empty() {
                        "Enter to write · Esc to cancel".to_string()
                    } else {
                        self.write_status.clone()
                    },
                    Style::new().fg(Color::DarkGray),
                )),
            ])
            .block(Block::new().borders(Borders::ALL).title("Write to clipboard"));
            frame.render_widget(popup, area);
        }

        // Help line.
        let keys = if self.write_mode {
            "Enter write · Esc cancel"
        } else {
            "Tab/←→ switch · 1-3 pages · r refresh · w write clipboard · s serve inbox · d clear history · q quit"
        };
        frame.render_widget(
            Paragraph::new(Line::from(Span::styled(keys, Style::new().fg(Color::DarkGray)))),
            help_area,
        );
    }

    fn render_dashboard(frame: &mut ratatui::Frame, model: &Model, area: ratatui::layout::Rect) {
        let chunks = ratatui::layout::Layout::vertical([
            ratatui::layout::Constraint::Percentage(45),
            ratatui::layout::Constraint::Percentage(55),
        ])
        .split(area);

        // Left: this device.
        let device_rows = vec![
            kv_row("name", model.device_label()),
            kv_row("hyprshare", model.version()),
            kv_row("state dir", model.state_dir()),
            kv_row("cpu", &format!("{:.1}%", model.snapshot.cpu)),
            kv_row("memory", &format!("{:.1}%", model.snapshot.memory)),
            kv_row("disk", &format!("{:.1}%", model.snapshot.disk)),
        ];
        let device = Table::new(
            device_rows,
            [ratatui::layout::Constraint::Length(14), ratatui::layout::Constraint::Min(10)],
        )
        .block(Block::new().borders(Borders::ALL).title("This device"))
        .column_spacing(1);
        frame.render_widget(device, chunks[0]);

        // Right: health failures and warnings only (like the GUI).
        let rows: Vec<Row> = model
            .health
            .results
            .iter()
            .filter(|c| c.status != "pass")
            .map(|c| {
                let style = match c.status.as_str() {
                    "fail" => Style::new().fg(Color::Red),
                    "warn" => Style::new().fg(Color::Yellow),
                    _ => Style::new().fg(Color::DarkGray),
                };
                Row::new(vec![
                    Cell::from(c.status.clone()).style(style),
                    Cell::from(c.id.clone()),
                    Cell::from(c.summary.clone()),
                ])
            })
            .collect();
        let summary = if rows.is_empty() {
            format!(" all {} checks pass ", model.health.checks)
        } else {
            format!(
                " {} pass · {} fail · {} warn ",
                model.health.pass, model.health.fail, model.health.warn
            )
        };
        let health = Table::new(
            rows,
            [
                ratatui::layout::Constraint::Length(6),
                ratatui::layout::Constraint::Length(26),
                ratatui::layout::Constraint::Min(20),
            ],
        )
        .block(Block::new().borders(Borders::ALL).title(format!("Health{summary}")))
        .column_spacing(1);
        frame.render_widget(health, chunks[1]);
    }

    fn render_inbox(frame: &mut ratatui::Frame, model: &Model, area: ratatui::layout::Rect) {
        let chunks = ratatui::layout::Layout::vertical([
            ratatui::layout::Constraint::Length(6),
            ratatui::layout::Constraint::Min(1),
        ])
        .split(area);

        let info = Table::new(
            vec![
                kv_row("receive dir", &model.inbox.resolved_receive_dir),
                kv_row(
                    "listen",
                    &format!("{}:{}", model.inbox.default_host, model.inbox.default_port),
                ),
                kv_row("writable", if model.inbox.writable { "yes" } else { "no" }),
            ],
            [ratatui::layout::Constraint::Length(14), ratatui::layout::Constraint::Min(10)],
        )
        .block(Block::new().borders(Borders::ALL).title("Inbox"))
        .column_spacing(1);
        frame.render_widget(info, chunks[0]);

        let rows: Vec<Row> = model
            .history
            .iter()
            .take(area.height.saturating_sub(4) as usize)
            .map(|e| {
                Row::new(vec![
                    Cell::from(e.file_name.clone()),
                    Cell::from(format!("{} KB", e.bytes_received / 1024)),
                    Cell::from(time_ago(e.received_at_unix)),
                ])
            })
            .collect();
        let history = Table::new(
            rows,
            [
                ratatui::layout::Constraint::Percentage(50),
                ratatui::layout::Constraint::Percentage(20),
                ratatui::layout::Constraint::Percentage(30),
            ],
        )
        .block(Block::new().borders(Borders::ALL).title(format!(
            "Transfer history · {}",
            model.history.len()
        )))
        .column_spacing(1);
        frame.render_widget(history, chunks[1]);
    }

    fn render_clipboard(frame: &mut ratatui::Frame, model: &Model, area: ratatui::layout::Rect) {
        let rows: Vec<Row> = model
            .clipboard_history
            .iter()
            .take(area.height.saturating_sub(2) as usize)
            .enumerate()
            .map(|(i, item)| {
                let text = {
                  let chars: Vec<char> = item.content.chars().collect();
                  if chars.len() > 60 {
                    format!("{}…", chars[..60].iter().collect::<String>())
                  } else {
                    item.content.clone()
                  }
                };
                Row::new(vec![
                    Cell::from(format!("{}", i + 1)).style(Style::new().fg(Color::DarkGray)),
                    Cell::from(text),
                ])
            })
            .collect();
        let table = Table::new(
            rows,
            [
                ratatui::layout::Constraint::Length(4),
                ratatui::layout::Constraint::Min(20),
            ],
        )
        .block(
            Block::new()
                .borders(Borders::ALL)
                .title(format!("Clipboard history · {}", model.clipboard_history.len())),
        )
        .column_spacing(1);
        frame.render_widget(table, area);
    }
}

fn model_error_hint(app: &App) -> Option<String> {
    app.model.as_ref().and_then(|model| {
        if model.load_errors.is_empty() {
            None
        } else {
            let shown = model.load_errors.iter().take(2).cloned().collect::<Vec<_>>().join(" · ");
            Some(format!("CLI data unavailable: {shown}"))
        }
    })
}

fn kv_row(key: &str, value: impl Into<String>) -> Row<'static> {
    Row::new(vec![
        Cell::from(key.to_string()).style(Style::new().fg(Color::Cyan)),
        Cell::from(value.into()),
    ])
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
