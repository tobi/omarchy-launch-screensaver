//! Raster ttfx's VT frame into its fixed-width cell grid.
//!
//! ttfx lays every emitted symbol out as one cell. Parsing with that invariant
//! avoids terminal-emulator Unicode-width policy changing the effect geometry.

#[repr(C)]
#[derive(Clone, Copy)]
pub struct TtfxCell {
    pub ch: u32,
    pub fg_r: u8,
    pub fg_g: u8,
    pub fg_b: u8,
    pub bg_r: u8,
    pub bg_g: u8,
    pub bg_b: u8,
}

impl Default for TtfxCell {
    fn default() -> Self {
        Self {
            ch: b' ' as u32,
            fg_r: 220,
            fg_g: 220,
            fg_b: 220,
            bg_r: 0,
            bg_g: 0,
            bg_b: 0,
        }
    }
}

pub fn raster(
    vt: &[u8],
    out: &mut [TtfxCell],
    cols: i32,
    rows: i32,
) -> Result<(), String> {
    let cols = cols.max(1);
    let rows = rows.max(1);
    let need = (cols as usize).saturating_mul(rows as usize);
    if out.len() < need {
        return Err("cell buffer too small".into());
    }
    out[..need].fill(TtfxCell::default());
    parse_width1(vt, out, cols, rows);
    Ok(())
}

fn parse_width1(data: &[u8], out: &mut [TtfxCell], cols: i32, rows: i32) {
    let mut x = 0i32;
    let mut y = 0i32;
    let mut fr = 220u8;
    let mut fg = 220u8;
    let mut fb = 220u8;
    let mut br = 0u8;
    let mut bg = 0u8;
    let mut bb = 0u8;
    let mut i = 0usize;

    while i < data.len() {
        let b = data[i];
        if b == 0x1b && i + 1 < data.len() && data[i + 1] == b'[' {
            i += 2;
            let mut nums = [0i32; 16];
            let mut num_count = 0usize;
            let mut cur = 0i32;
            let mut have = false;
            while i < data.len() {
                let c = data[i];
                i += 1;
                if c.is_ascii_digit() {
                    cur = cur * 10 + i32::from(c - b'0');
                    have = true;
                } else if c == b';' {
                    if num_count < nums.len() {
                        nums[num_count] = if have { cur } else { 0 };
                        num_count += 1;
                    }
                    cur = 0;
                    have = false;
                } else {
                    if have && num_count < nums.len() {
                        nums[num_count] = cur;
                        num_count += 1;
                    }
                    match c {
                        b'm' => {
                            if num_count == 0 {
                                num_count = 1;
                            }
                            let mut k = 0;
                            while k < num_count {
                                match nums[k] {
                                    0 => {
                                        fr = 220;
                                        fg = 220;
                                        fb = 220;
                                        br = 0;
                                        bg = 0;
                                        bb = 0;
                                    }
                                    38 if k + 4 < num_count && nums[k + 1] == 2 => {
                                        fr = nums[k + 2] as u8;
                                        fg = nums[k + 3] as u8;
                                        fb = nums[k + 4] as u8;
                                        k += 4;
                                    }
                                    48 if k + 4 < num_count && nums[k + 1] == 2 => {
                                        br = nums[k + 2] as u8;
                                        bg = nums[k + 3] as u8;
                                        bb = nums[k + 4] as u8;
                                        k += 4;
                                    }
                                    _ => {}
                                }
                                k += 1;
                            }
                        }
                        b'H' | b'f' => {
                            let row = if num_count > 0 { nums[0] } else { 1 };
                            let col = if num_count >= 2 { nums[1] } else { 1 };
                            y = (row - 1).max(0);
                            x = (col - 1).max(0);
                        }
                        b'J' => {
                            out.fill(TtfxCell::default());
                            x = 0;
                            y = 0;
                        }
                        b'K' => {
                            if y >= 0 && y < rows {
                                let start = (y as usize) * (cols as usize);
                                let from = start + (x.max(0) as usize).min(cols as usize);
                                let to = start + cols as usize;
                                out[from..to].fill(TtfxCell::default());
                            }
                        }
                        _ => {}
                    }
                    break;
                }
            }
            continue;
        }
        if b == b'\n' {
            y += 1;
            x = 0;
            i += 1;
            continue;
        }
        if b == b'\r' {
            x = 0;
            i += 1;
            continue;
        }
        if b < 0x20 {
            i += 1;
            continue;
        }
        let (ch, n) = utf8_at(data, i);
        i += n;
        put_cell(out, cols, rows, x, y, ch, fr, fg, fb, br, bg, bb);
        x += 1;
    }
}

fn put_cell(
    out: &mut [TtfxCell],
    cols: i32,
    rows: i32,
    x: i32,
    y: i32,
    ch: u32,
    fr: u8,
    fg: u8,
    fb: u8,
    br: u8,
    bg: u8,
    bb: u8,
) {
    if y >= 0 && y < rows && x >= 0 && x < cols {
        let cell = &mut out[(y as usize) * (cols as usize) + (x as usize)];
        cell.ch = if ch == 0 { b' ' as u32 } else { ch };
        cell.fg_r = fr;
        cell.fg_g = fg;
        cell.fg_b = fb;
        cell.bg_r = br;
        cell.bg_g = bg;
        cell.bg_b = bb;
    }
}

fn utf8_at(data: &[u8], i: usize) -> (u32, usize) {
    let b = data[i];
    if b < 0x80 {
        return (u32::from(b), 1);
    }
    let (need, mut cp) = if b & 0xE0 == 0xC0 {
        (1, u32::from(b & 0x1F))
    } else if b & 0xF0 == 0xE0 {
        (2, u32::from(b & 0x0F))
    } else if b & 0xF8 == 0xF0 {
        (3, u32::from(b & 0x07))
    } else {
        return (u32::from(b), 1);
    };
    let mut n = 1;
    for _ in 0..need {
        if i + n >= data.len() {
            break;
        }
        cp = (cp << 6) | u32::from(data[i + n] & 0x3F);
        n += 1;
    }
    (cp, n)
}
