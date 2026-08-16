use std::{env, process::ExitCode};

use anyhow::{Context, Result};
use omarchy_launch_screensaver::{engine::Animation, launch, options, options::Options, wayland};

fn main() -> ExitCode {
    let parsed = match options::parse() {
        Ok(parsed) => parsed,
        Err(error) => {
            eprintln!("omarchy-launch-screensaver: {error}");
            return ExitCode::from(2);
        }
    };
    let Some(options) = parsed else {
        options::print_help();
        return ExitCode::SUCCESS;
    };
    let input = match options::load_input(&options) {
        Ok(input) => input,
        Err(error) => {
            eprintln!("omarchy-launch-screensaver: {error:#}");
            return ExitCode::FAILURE;
        }
    };
    if options.headless {
        return report(run_headless(options, input));
    }
    report(run_overlay(options, input))
}

fn report(result: Result<i32>) -> ExitCode {
    match result {
        Ok(code) => ExitCode::from(code.clamp(0, 255) as u8),
        Err(error) => {
            eprintln!("omarchy-launch-screensaver: {error:#}");
            ExitCode::FAILURE
        }
    }
}

fn run_overlay(options: Options, input: String) -> Result<i32> {
    if launch::already_running() {
        return Ok(0);
    }
    let Some(_instance_lock) = launch::acquire_lock() else {
        return Ok(0);
    };
    if launch::toggled_off() && !options.force {
        return Ok(1);
    }
    if launch::session_locked() && !options.force {
        return Ok(0);
    }
    if env::var_os("WAYLAND_DISPLAY").is_none() && env::var_os("WAYLAND_SOCKET").is_none() {
        anyhow::bail!("no WAYLAND_DISPLAY (use --headless on this box)");
    }
    launch::quiet_walker();
    let _cursor = launch::CursorGuard::hide();
    wayland::run(input, options).context("screensaver failed")?;
    Ok(0)
}

fn run_headless(options: Options, input: String) -> Result<i32> {
    let cols = if options.cols > 0 { options.cols } else { 80 };
    let rows = if options.rows > 0 { options.rows } else { 24 };
    let frame_count = options.frames;
    let mut animation = Animation::new(input, options, cols, rows)?;
    let mut frames = 0;
    while frames < frame_count {
        animation.advance()?;
        frames += 1;
    }
    let mut non_space = 0usize;
    let mut empty_rows = 0usize;
    for row in animation.cells().chunks(animation.cols() as usize) {
        let occupied = row
            .iter()
            .filter(|cell| cell.ch != ' ' && cell.ch != '\0')
            .count();
        non_space += occupied;
        empty_rows += usize::from(occupied == 0);
    }
    println!(
        "frames={frames} cells={}x{} non_space={non_space} empty_rows={empty_rows} effect={} backend=rust",
        animation.cols(),
        animation.rows(),
        animation.effect_name(),
    );
    if env::var_os("SSAVER_DUMP").is_some() {
        for row in animation.cells().chunks(animation.cols() as usize) {
            for cell in row {
                print!("{}", if cell.ch.is_control() { ' ' } else { cell.ch });
            }
            println!();
        }
    }
    Ok(0)
}
