use std::{env, fs, path::PathBuf};

use anyhow::{Context, Result, bail};

#[derive(Clone, Debug)]
pub struct Options {
    pub force: bool,
    pub headless: bool,
    pub input_path: Option<PathBuf>,
    pub effect: Option<String>,
    pub cols: i32,
    pub rows: i32,
    pub frames: usize,
    pub frame_rate: i32,
    pub fade_ms: u64,
    pub fade_out_ms: u64,
    pub canvas_width: i32,
    pub canvas_height: i32,
    pub reuse_canvas: bool,
    pub anchor_canvas: String,
    pub anchor_text: String,
    pub no_eol: bool,
    pub no_restore_cursor: bool,
    pub seed: Option<u64>,
    pub include_effects: Vec<String>,
    pub exclude_effects: Vec<String>,
}

impl Default for Options {
    fn default() -> Self {
        Self {
            force: false,
            headless: false,
            input_path: None,
            effect: None,
            cols: 0,
            rows: 0,
            frames: 30,
            frame_rate: 120,
            fade_ms: 1000,
            fade_out_ms: 200,
            canvas_width: 0,
            canvas_height: 0,
            reuse_canvas: true,
            anchor_canvas: "c".into(),
            anchor_text: "c".into(),
            no_eol: true,
            no_restore_cursor: true,
            seed: None,
            include_effects: Vec::new(),
            exclude_effects: Vec::new(),
        }
    }
}

pub fn parse() -> Result<Option<Options>> {
    parse_from(env::args().skip(1))
}

fn parse_from(args: impl IntoIterator<Item = String>) -> Result<Option<Options>> {
    let args: Vec<String> = args.into_iter().collect();
    let mut out = Options::default();
    let mut i = 0;
    while i < args.len() {
        let raw = &args[i];
        let (key, inline) = raw
            .strip_prefix("--")
            .and_then(|rest| {
                rest.split_once('=')
                    .map(|(k, v)| (format!("--{k}"), Some(v)))
            })
            .unwrap_or_else(|| (raw.clone(), None));
        let mut take = |name: &str| -> Result<String> {
            if let Some(value) = inline {
                return Ok(value.to_owned());
            }
            i += 1;
            args.get(i)
                .cloned()
                .with_context(|| format!("{name} needs a value"))
        };
        let int = |name: &str, value: String| -> Result<i32> {
            value
                .parse()
                .with_context(|| format!("{name} expects an integer, got '{value}'"))
        };
        match key.as_str() {
            "force" | "--force" => out.force = true,
            "-h" | "--help" => return Ok(None),
            "-i" | "--input" | "--input-file" => out.input_path = Some(take("--input")?.into()),
            "--effect" => out.effect = Some(take("--effect")?),
            "-R" | "--random-effect" | "--random" => out.effect = None,
            "--include-effects" => out.include_effects = split_list(&take("--include-effects")?),
            "--exclude-effects" => out.exclude_effects = split_list(&take("--exclude-effects")?),
            "--frame-rate" => out.frame_rate = int("--frame-rate", take("--frame-rate")?)?,
            "--fade" => {
                let value = take("--fade")?;
                let mut parts = value.split([',', ':']);
                out.fade_ms = positive("--fade", parts.next().unwrap_or_default())?;
                if let Some(value) = parts.next() {
                    out.fade_out_ms = positive("--fade OUT", value)?;
                }
                if parts.next().is_some() {
                    bail!("--fade needs N or N,OUT");
                }
            }
            "--canvas-width" => out.canvas_width = int("--canvas-width", take("--canvas-width")?)?,
            "--canvas-height" => {
                out.canvas_height = int("--canvas-height", take("--canvas-height")?)?
            }
            "--reuse-canvas" => out.reuse_canvas = true,
            "--anchor-canvas" => out.anchor_canvas = take("--anchor-canvas")?,
            "--anchor-text" => out.anchor_text = take("--anchor-text")?,
            "--no-eol" => out.no_eol = true,
            "--no-restore-cursor" => out.no_restore_cursor = true,
            "--seed" => {
                let value = take("--seed")?;
                out.seed = Some(
                    value
                        .parse()
                        .with_context(|| format!("--seed expects an integer, got '{value}'"))?,
                );
            }
            "--headless" => out.headless = true,
            "--frames" => {
                let value = int("--frames", take("--frames")?)?;
                out.frames = value.max(0) as usize;
            }
            "--cols" => out.cols = int("--cols", take("--cols")?)?,
            "--rows" => out.rows = int("--rows", take("--rows")?)?,
            _ => bail!("unknown arg {raw}"),
        }
        i += 1;
    }
    if out.frame_rate <= 0 {
        out.frame_rate = 120;
    }
    Ok(Some(out))
}

fn positive(name: &str, value: &str) -> Result<u64> {
    let parsed: u64 = value
        .parse()
        .with_context(|| format!("{name} expects a positive integer"))?;
    if parsed == 0 {
        bail!("{name} expects a positive integer");
    }
    Ok(parsed)
}

fn split_list(value: &str) -> Vec<String> {
    value
        .split(|c: char| c == ',' || c.is_whitespace())
        .filter(|part| !part.is_empty())
        .map(str::to_owned)
        .collect()
}

pub fn load_input(options: &Options) -> Result<String> {
    if let Some(path) = &options.input_path {
        let value = fs::read_to_string(path)
            .with_context(|| format!("cannot read --input {}", path.display()))?;
        if value.is_empty() {
            bail!("cannot read --input {}", path.display());
        }
        return Ok(value);
    }
    if let Some(home) = env::var_os("HOME") {
        let path = PathBuf::from(home).join(".config/omarchy/branding/screensaver.txt");
        if let Ok(value) = fs::read_to_string(path)
            && !value.is_empty()
        {
            return Ok(value);
        }
    }
    Ok(include_str!("../assets/screensaver.txt").to_owned())
}

pub fn print_help() {
    print!(
        r#"omarchy-launch-screensaver — Omarchy screensaver (Rust + Wayland + ttfx)

Usage:
  omarchy-launch-screensaver [force] [options]

Native layer-shell overlay per output. No terminal, Qt, C++, or ttfx child.
Frames are committed only after content invalidation and compositor readiness.

  force, --force          launch even if screensaver toggle is off
  -h, --help              show this help
  -i, --input PATH        input text (also --input-file)
  --effect NAME           pin one effect (still loops it)
  -R, --random-effect     pick a random effect (default)
  --include-effects LIST  limit random pick (comma-separated)
  --exclude-effects LIST  skip these when random
  --frame-rate N          ttfx simulation rate (default 120)
  --fade N[,OUT]          appear ms (default 1000), dismiss ms (default 200)
  --canvas-width N        0 = output width
  --canvas-height N       0 = output height
  --reuse-canvas          keep canvas between frames (default on)
  --anchor-canvas c|...   canvas anchor (default c)
  --anchor-text c|...     text anchor (default c)
  --no-eol                omit trailing newline (default on)
  --no-restore-cursor     leave cursor hidden (default on)
  --seed N                deterministic RNG
  --headless              no Wayland; for tests
  --frames N              headless only (default 30)
  --cols N --rows N       headless canvas (default 80x24)

Input file default: ~/.config/omarchy/branding/screensaver.txt
then the bundled assets/screensaver.txt.
"#
    );
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_force_effect_and_fade() {
        let options = parse_from([
            "force".into(),
            "--effect=print".into(),
            "--fade".into(),
            "1000,200".into(),
        ])
        .unwrap()
        .expect("unexpected help");
        assert!(options.force);
        assert_eq!(options.effect.as_deref(), Some("print"));
        assert_eq!((options.fade_ms, options.fade_out_ms), (1000, 200));
    }
}
