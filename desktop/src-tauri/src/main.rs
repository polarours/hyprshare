use std::fs;
use std::path::PathBuf;
use std::process::Command;

use serde::Serialize;

/// Run a hyprshare CLI command and return its stdout. Every GUI action is a
/// thin pass-through to the installed CLI, so the GUI and the command line
/// can never disagree about behaviour.
#[tauri::command]
fn cli(args: Vec<String>) -> Result<String, String> {
    if args.is_empty() {
        return Err("no command given".into());
    }
    let output = Command::new("hyprshare")
        .args(&args)
        .output()
        .map_err(|e| format!("failed to run hyprshare: {e}"))?;
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
    let receive_dir = Command::new("hyprshare")
        .args(["config", "get", "files.receive_dir"])
        .output()
        .map_err(|e| e.to_string())?;
    if !receive_dir.status.success() {
        return Err("failed to read the receive directory".into());
    }
    let raw = String::from_utf8_lossy(&receive_dir.stdout).trim().to_string();
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
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![cli, upload_files, app_info])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
