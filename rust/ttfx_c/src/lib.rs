//! Tiny C ABI over the ttfx engine. Drives Effect::build / next_frame only.
//! Never calls run_effect (that writes DEC cursor junk to stdout).

use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::ptr;
use std::sync::Mutex;

use clap::{CommandFactory, Parser};
use ttfx::cli::Cli;
use ttfx::engine::canvas::Anchor;
use ttfx::engine::ctx::{Clock, EngineCtx};
use ttfx::engine::effect::Effect;
use ttfx::engine::terminal::TerminalConfig;
use ttfx::utils::rng::Rng;

pub struct Engine {
    ctx: EngineCtx,
    effect: Box<dyn Effect>,
    name: CString,
    last_frame: Vec<u8>,
}

static LAST_ERROR: Mutex<Option<CString>> = Mutex::new(None);
static EFFECT_NAMES: Mutex<Option<CString>> = Mutex::new(None);

fn set_error(msg: impl Into<String>) {
    let s = CString::new(msg.into()).unwrap_or_else(|_| CString::new("error").unwrap());
    *LAST_ERROR.lock().unwrap() = Some(s);
}

fn cstr<'a>(p: *const c_char) -> Option<&'a str> {
    if p.is_null() {
        return None;
    }
    unsafe { CStr::from_ptr(p) }.to_str().ok().filter(|s| !s.is_empty())
}

fn split_list(s: Option<&str>) -> Vec<String> {
    match s {
        None => Vec::new(),
        Some(s) => s
            .split(|c: char| c == ',' || c.is_whitespace())
            .filter(|x| !x.is_empty())
            .map(|x| x.to_string())
            .collect(),
    }
}

fn effect_names() -> Vec<String> {
    Cli::command()
        .get_subcommands()
        .map(|c| c.get_name().to_string())
        .collect()
}

fn effect_by_name(name: &str) -> Result<ttfx::effects::EffectCommand, String> {
    match Cli::try_parse_from(["ttfx", name]) {
        Ok(Cli {
            effect: Some(effect),
            ..
        }) => Ok(effect),
        Ok(_) => Err(format!("no effect in parse of '{name}'")),
        Err(e) => Err(format!("unknown effect '{name}': {e}")),
    }
}

fn pick_effect(
    requested: Option<&str>,
    include: &[String],
    exclude: &[String],
    rng: &mut Rng,
) -> Result<(String, ttfx::effects::EffectCommand), String> {
    let all = effect_names();
    if all.is_empty() {
        return Err("ttfx has no effects".into());
    }

    let name = match requested {
        None | Some("") | Some("random") => {
            for n in include {
                if !all.iter().any(|x| x == n) {
                    return Err(format!("unknown effect in --include-effects: '{n}'"));
                }
            }
            for n in exclude {
                if !all.iter().any(|x| x == n) {
                    return Err(format!("unknown effect in --exclude-effects: '{n}'"));
                }
            }
            let mut names = all;
            if !include.is_empty() {
                names.retain(|n| include.iter().any(|i| i == n));
            }
            if !exclude.is_empty() {
                names.retain(|n| !exclude.iter().any(|e| e == n));
            }
            if names.is_empty() {
                return Err("no effects left after include/exclude".into());
            }
            names[rng.choice_index(names.len())].clone()
        }
        Some(n) => {
            if !all.iter().any(|x| x == n) {
                return Err(format!("unknown effect '{n}' (have: {})", all.join(", ")));
            }
            n.to_string()
        }
    };
    let cmd = effect_by_name(&name)?;
    Ok((name, cmd))
}

fn parse_anchor(s: Option<&str>, default: Anchor) -> Anchor {
    match s {
        None | Some("") => default,
        Some(v) => Anchor::parse(v).unwrap_or(default),
    }
}

fn make_config(cols: i32, rows: i32, cfg: Option<&TtfxConfig>) -> TerminalConfig {
    let mut config = TerminalConfig::default();
    let frame_rate = cfg.map(|c| c.frame_rate).unwrap_or(120);
    config.frame_rate = if frame_rate > 0 { frame_rate as i64 } else { 120 };
    let cw = cfg.map(|c| c.canvas_width).unwrap_or(0);
    let ch = cfg.map(|c| c.canvas_height).unwrap_or(0);
    // Positive canvas = exact size. 0 = output/terminal cells (cols/rows).
    // Do not use -1 (input size clamped to 80x24 when no tty).
    config.canvas_width = if cw > 0 { cw as i64 } else { cols.max(1) as i64 };
    config.canvas_height = if ch > 0 { ch as i64 } else { rows.max(1) as i64 };
    config.ignore_terminal_dimensions = true;
    config.reuse_canvas = cfg.map(|c| c.reuse_canvas != 0).unwrap_or(true);
    config.no_eol = cfg.map(|c| c.no_eol != 0).unwrap_or(true);
    config.no_restore_cursor = cfg.map(|c| c.no_restore_cursor != 0).unwrap_or(true);
    config.anchor_canvas = parse_anchor(cfg.and_then(|c| cstr(c.anchor_canvas)), Anchor::C);
    config.anchor_text = parse_anchor(cfg.and_then(|c| cstr(c.anchor_text)), Anchor::C);
    config
}

fn create_engine(
    input: &str,
    cols: i32,
    rows: i32,
    requested: Option<&str>,
    cfg: Option<&TtfxConfig>,
) -> Result<Engine, String> {
    let include = split_list(cfg.and_then(|c| cstr(c.include_effects)));
    let exclude = split_list(cfg.and_then(|c| cstr(c.exclude_effects)));
    let mut rng = match cfg {
        Some(c) if c.has_seed != 0 => Rng::seeded(c.seed),
        _ => Rng::from_entropy(),
    };
    let (name, cmd) = pick_effect(requested, &include, &exclude, &mut rng)?;
    let config = make_config(cols, rows, cfg);
    let clock = Clock::virtual_with_frame_rate(config.frame_rate);
    let mut ctx = EngineCtx::new(input, config, rng, clock).map_err(|e| format!("EngineCtx::new: {e}"))?;
    let mut eff = cmd.build_effect();
    eff.build(&mut ctx).map_err(|e| format!("effect.build: {e}"))?;
    Ok(Engine {
        ctx,
        effect: eff,
        name: CString::new(name).unwrap(),
        last_frame: Vec::new(),
    })
}

#[repr(C)]
pub struct TtfxConfig {
    pub input: *const c_char,
    pub cols: i32,
    pub rows: i32,
    pub effect: *const c_char,
    pub frame_rate: i32,
    pub canvas_width: i32,
    pub canvas_height: i32,
    pub reuse_canvas: i32,
    pub anchor_canvas: *const c_char,
    pub anchor_text: *const c_char,
    pub no_eol: i32,
    pub no_restore_cursor: i32,
    pub has_seed: i32,
    pub seed: u64,
    pub include_effects: *const c_char,
    pub exclude_effects: *const c_char,
}

#[no_mangle]
pub extern "C" fn ttfx_create(
    input: *const c_char,
    cols: i32,
    rows: i32,
    effect: *const c_char,
) -> *mut Engine {
    let input = cstr(input).unwrap_or("omarchy");
    let requested = cstr(effect);
    match create_engine(input, cols, rows, requested, None) {
        Ok(eng) => Box::into_raw(Box::new(eng)),
        Err(e) => {
            set_error(e);
            ptr::null_mut()
        }
    }
}

#[no_mangle]
pub extern "C" fn ttfx_create_ex(cfg: *const TtfxConfig) -> *mut Engine {
    if cfg.is_null() {
        set_error("null TtfxConfig");
        return ptr::null_mut();
    }
    let cfg = unsafe { &*cfg };
    let input = cstr(cfg.input).unwrap_or("omarchy");
    let requested = cstr(cfg.effect);
    match create_engine(input, cfg.cols, cfg.rows, requested, Some(cfg)) {
        Ok(eng) => Box::into_raw(Box::new(eng)),
        Err(e) => {
            set_error(e);
            ptr::null_mut()
        }
    }
}

#[no_mangle]
pub extern "C" fn ttfx_destroy(eng: *mut Engine) {
    if !eng.is_null() {
        unsafe {
            drop(Box::from_raw(eng));
        }
    }
}

#[no_mangle]
pub extern "C" fn ttfx_next_frame(
    eng: *mut Engine,
    data: *mut *const u8,
    len: *mut usize,
) -> i32 {
    if eng.is_null() || data.is_null() || len.is_null() {
        set_error("null argument");
        return -1;
    }
    let eng = unsafe { &mut *eng };
    match eng.effect.next_frame(&mut eng.ctx) {
        Some(frame) => {
            // recycle_output_string is pub(crate) — we own the bytes instead.
            eng.last_frame = frame.into_bytes();
            unsafe {
                *data = eng.last_frame.as_ptr();
                *len = eng.last_frame.len();
            }
            1
        }
        None => {
            unsafe {
                *data = ptr::null();
                *len = 0;
            }
            0
        }
    }
}

#[no_mangle]
pub extern "C" fn ttfx_effect_name(eng: *const Engine) -> *const c_char {
    if eng.is_null() {
        return ptr::null();
    }
    unsafe { (*eng).name.as_ptr() }
}

#[no_mangle]
pub extern "C" fn ttfx_last_error() -> *const c_char {
    match LAST_ERROR.lock().unwrap().as_ref() {
        Some(s) => s.as_ptr(),
        None => ptr::null(),
    }
}

#[no_mangle]
pub extern "C" fn ttfx_effect_names() -> *const c_char {
    let names = effect_names().join(",");
    let s = CString::new(names).unwrap_or_else(|_| CString::new("").unwrap());
    let ptr = s.as_ptr();
    *EFFECT_NAMES.lock().unwrap() = Some(s);
    ptr
}
