use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::OnceLock;
use std::time::{SystemTime, UNIX_EPOCH};

use serde::Serialize;

/// Where the CLI lives. The GUI is frequently launched from a desktop entry or
/// a launcher, whose PATH may not include the developer build directory, so
/// resolving "hyprshare" through PATH alone made every action fail with
/// "failed to run hyprshare" when started outside a shell.
fn hyprshare_binary() -> &'static str {
    static RESOLVED: OnceLock<String> = OnceLock::new();
    RESOLVED.get_or_init(|| {
        let mut candidates: Vec<PathBuf> = Vec::new();

        // 1. An explicit override always wins.
        if let Ok(explicit) = std::env::var("HYPRSHARE_BIN") {
            if !explicit.is_empty() {
                candidates.push(PathBuf::from(explicit));
            }
        }

        // 2. Next to this executable, then up to four ancestors (covers both
        //    an installed /usr/bin layout and a build-tree layout).
        if let Ok(exe) = std::env::current_exe() {
            let mut dir = exe.parent().map(Path::to_path_buf);
            for _ in 0..5 {
                let Some(current) = dir else { break };
                candidates.push(current.join("hyprshare"));
                candidates.push(current.join("build").join("hyprshare"));
                dir = current.parent().map(Path::to_path_buf);
            }
        }

        // 3. The usual install locations.
        candidates.push(PathBuf::from("/usr/bin/hyprshare"));
        candidates.push(PathBuf::from("/usr/local/bin/hyprshare"));

        for candidate in candidates {
            if candidate.is_file() {
                return candidate.to_string_lossy().into_owned();
            }
        }

        // 4. Fall back to PATH resolution by name.
        "hyprshare".to_string()
    })
}

/// Run a hyprshare CLI command and return its stdout. Every GUI action is a
/// thin pass-through to the installed CLI, so the GUI and the command line
/// can never disagree about behaviour.
#[tauri::command]
fn cli(args: Vec<String>) -> Result<String, String> {
    if args.is_empty() {
        return Err("no command given".into());
    }
    let binary = hyprshare_binary();
    let output = Command::new(binary)
        .args(&args)
        .output()
        .map_err(|e| {
            format!(
                "failed to run {binary} (searched HYPRSHARE_BIN, the app directory, ./build and /usr/bin): {e}"
            )
        })?;
    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr).trim().to_string();
        return Err(if stderr.is_empty() {
            format!("hyprshare {} exited with {}", args[0], output.status)
        } else {
            stderr
        });
    }
    String::from_utf8(output.stdout).map_err(|e| e.to_string())
}

#[derive(serde::Deserialize)]
struct UploadFile {
    name: String,
    bytes: Vec<u8>,
}

/// Write files straight into the configured inbox receive directory. The web
/// REST server does the same through multipart; the desktop build has no
/// server, so the bytes come over invoke instead.
#[tauri::command]
fn upload_files(files: Vec<UploadFile>) -> Result<usize, String> {
    let receive_dir = Command::new(hyprshare_binary())
        .args(["config", "get", "files.receive_dir"])
        .output()
        .map_err(|e| e.to_string())?;
    if !receive_dir.status.success() {
        return Err("failed to read the receive directory".into());
    }
    let raw = String::from_utf8_lossy(&receive_dir.stdout)
        .trim()
        .to_string();
    let expanded = if let Some(rest) = raw.strip_prefix("~/") {
        PathBuf::from(std::env::var("HOME").map_err(|e| e.to_string())?).join(rest)
    } else {
        PathBuf::from(&raw)
    };
    fs::create_dir_all(&expanded).map_err(|e| e.to_string())?;

    let mut written = 0;
    for file in &files {
        // Never let a file name escape the receive directory.
        let name = PathBuf::from(&file.name)
            .file_name()
            .map(|n| n.to_string_lossy().to_string())
            .unwrap_or_else(|| "upload".into());
        fs::write(expanded.join(name), &file.bytes).map_err(|e| e.to_string())?;
        written += 1;
    }
    Ok(written)
}

#[derive(Serialize)]
pub struct ServeSessionInfo {
    token: String,
    port: u16,
    pid: u32,
    address: String,
}

/// Start serving a staged share session in the background with a fresh token.
/// `session serve` blocks while waiting for a receiver, so it must be spawned
/// detached — otherwise the invoke call (and the UI) would hang forever.
// Find the local LAN address without sending packets (UDP connect only picks
// a route; no data leaves the machine). Fall back to loopback if unavailable.
fn local_lan_ip() -> String {
    use std::net::UdpSocket;
    UdpSocket::bind("0.0.0.0:0")
        .and_then(|socket| {
            socket.connect("8.8.8.8:80")?;
            socket.local_addr()
        })
        .map(|addr| addr.ip().to_string())
        .unwrap_or_else(|_| "127.0.0.1".to_string())
}

#[tauri::command]
fn serve_session(session_id: String, port: u16) -> Result<ServeSessionInfo, String> {
    if session_id.is_empty() {
        return Err("no session selected".into());
    }
    // 32 hex chars from clock + pid: enough entropy for a short-lived token.
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|e| e.to_string())?
        .as_nanos();
    let token = format!(
        "{:016x}{:08x}{:08x}",
        nanos,
        std::process::id(),
        nanos >> 64
    );

    let child = Command::new(hyprshare_binary())
        .args([
            "session",
            "serve",
            &session_id,
            "--host",
            "0.0.0.0",
            "--port",
            &port.to_string(),
            "--token",
            &token,
        ])
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .map_err(|e| format!("failed to start session serve: {e}"))?;

    Ok(ServeSessionInfo {
        token,
        port,
        pid: child.id(),
        address: format!("{}:{}", local_lan_ip(), port),
    })
}

#[derive(Serialize)]
struct AppInfo {
    version: String,
}

#[tauri::command]
fn app_info() -> AppInfo {
    AppInfo {
        version: env!("CARGO_PKG_VERSION").to_string(),
    }
}

fn main() {
    // Diagnostic entry point: `hyprshare-desktop --print-cli-path` prints which
    // CLI binary the GUI would call and exits. Used to prove the resolution
    // works when the app is started outside a shell.
    if std::env::args().any(|arg| arg == "--print-cli-path") {
        println!("{}", hyprshare_binary());
        return;
    }

    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .invoke_handler(tauri::generate_handler![
            cli,
            upload_files,
            app_info,
            serve_session
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
