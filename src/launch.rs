use std::{
    env, fs,
    fs::{File, OpenOptions},
    io::Write,
    path::{Path, PathBuf},
    process::{Command, Stdio},
};

use fs2::FileExt;

const APP_ID: &[u8] = b"org.omarchy.screensaver";

pub fn already_running() -> bool {
    let own_pid = std::process::id().to_string();
    let Ok(entries) = fs::read_dir("/proc") else {
        return false;
    };
    entries.flatten().any(|entry| {
        let name = entry.file_name();
        if name == own_pid.as_str() || !name.as_encoded_bytes().iter().all(u8::is_ascii_digit) {
            return false;
        }
        fs::read(entry.path().join("cmdline"))
            .is_ok_and(|bytes| bytes.windows(APP_ID.len()).any(|window| window == APP_ID))
    })
}

pub fn acquire_lock() -> Option<File> {
    let path = env::var_os("XDG_RUNTIME_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("/tmp"))
        .join("omarchy-screensaver.lock");
    let mut file = match OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .truncate(false)
        .open(path)
    {
        Ok(file) => file,
        Err(_) => return OpenOptions::new().read(true).open("/dev/null").ok(),
    };
    if FileExt::try_lock_exclusive(&file).is_err() {
        return None;
    }
    let _ = file.set_len(0);
    let _ = writeln!(file, "{}", std::process::id());
    Some(file)
}

pub fn toggled_off() -> bool {
    if command_exists("omarchy-toggle-enabled") {
        return Command::new("omarchy-toggle-enabled")
            .arg("screensaver-off")
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status()
            .is_ok_and(|status| status.success());
    }
    env::var_os("HOME").map(PathBuf::from).is_some_and(|home| {
        home.join(".local/state/omarchy/toggles/screensaver-off")
            .exists()
    })
}

pub fn session_locked() -> bool {
    if !command_exists("omarchy-shell") {
        return false;
    }
    let output = if command_exists("timeout") {
        Command::new("timeout")
            .args(["--kill-after=1s", "2s", "omarchy-shell", "lock", "isLocked"])
            .stderr(Stdio::null())
            .output()
    } else {
        Command::new("omarchy-shell")
            .args(["lock", "isLocked"])
            .stderr(Stdio::null())
            .output()
    };
    output.is_ok_and(|output| {
        matches!(
            String::from_utf8_lossy(&output.stdout).trim(),
            "true" | "True" | "1"
        )
    })
}

pub fn quiet_walker() {
    if command_exists("walker") {
        let _ = Command::new("walker")
            .arg("-q")
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status();
    }
}

pub struct CursorGuard;

impl CursorGuard {
    pub fn hide() -> Self {
        set_cursor(true);
        Self
    }
}

impl Drop for CursorGuard {
    fn drop(&mut self) {
        set_cursor(false);
    }
}

fn set_cursor(invisible: bool) {
    if !command_exists("hyprctl") {
        return;
    }
    let expression = format!(
        "hl.config({{ cursor = {{ invisible = {} }} }})",
        if invisible { "true" } else { "false" }
    );
    let status = Command::new("hyprctl")
        .args(["eval", &expression])
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status();
    if !status.is_ok_and(|status| status.success()) {
        let _ = Command::new("hyprctl")
            .args([
                "keyword",
                "cursor:invisible",
                if invisible { "true" } else { "false" },
            ])
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status();
    }
}

fn command_exists(name: &str) -> bool {
    if name.contains('/') {
        return Path::new(name).is_file();
    }
    env::var_os("PATH")
        .is_some_and(|path| env::split_paths(&path).any(|directory| directory.join(name).is_file()))
}
