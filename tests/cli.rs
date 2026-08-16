use std::process::{Command, Stdio};

fn binary() -> Command {
    Command::new(env!("CARGO_BIN_EXE_omarchy-launch-screensaver"))
}

#[test]
fn help_describes_native_rust_contract() {
    let output = binary().arg("--help").output().unwrap();
    assert!(output.status.success());
    let stdout = String::from_utf8(output.stdout).unwrap();
    assert!(stdout.contains("Rust + Wayland + ttfx"));
    assert!(stdout.contains("--frame-rate N          ttfx simulation rate (default 120)"));
    assert!(stdout.contains("No terminal, Qt, C++, or ttfx child"));
}

#[test]
fn seeded_headless_render_has_content() {
    let output = binary()
        .args([
            "--headless",
            "--frames",
            "80",
            "--effect",
            "print",
            "--cols",
            "40",
            "--rows",
            "12",
            "--seed",
            "1",
            "--input",
            "assets/screensaver.txt",
        ])
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let stdout = String::from_utf8(output.stdout).unwrap();
    assert!(stdout.contains("frames=80 cells=40x12"));
    assert!(stdout.contains("backend=rust"));
    assert!(!stdout.contains("non_space=0"));
}

#[test]
fn headless_render_survives_closed_standard_streams() {
    let status = binary()
        .args([
            "--headless",
            "--frames",
            "10",
            "--effect",
            "print",
            "--cols",
            "40",
            "--rows",
            "12",
            "--seed",
            "1",
            "--input",
            "assets/screensaver.txt",
        ])
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .unwrap();
    assert!(status.success());
}

#[test]
fn invalid_argument_returns_usage_error() {
    let output = binary().arg("--not-an-option").output().unwrap();
    assert_eq!(output.status.code(), Some(2));
    assert!(String::from_utf8_lossy(&output.stderr).contains("unknown arg --not-an-option"));
}
