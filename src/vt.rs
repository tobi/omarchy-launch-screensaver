//! Parse ttfx's complete VT frame into its fixed-width cell grid.
//!
//! ttfx intentionally emits every symbol as one cell. Keeping that invariant here
//! avoids a second terminal emulator and its conflicting Unicode-width policy.

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Cell {
    pub ch: char,
    pub fg: [u8; 3],
    pub bg: [u8; 3],
}

impl Default for Cell {
    fn default() -> Self {
        Self {
            ch: ' ',
            fg: [220; 3],
            bg: [0; 3],
        }
    }
}

pub fn raster(vt: &[u8], out: &mut [Cell], cols: i32, rows: i32) -> Result<(), &'static str> {
    let cols = cols.max(1);
    let rows = rows.max(1);
    let need = (cols as usize).saturating_mul(rows as usize);
    if out.len() < need {
        return Err("cell buffer too small");
    }
    out[..need].fill(Cell::default());
    parse_width1(vt, out, cols, rows);
    Ok(())
}

fn parse_width1(data: &[u8], out: &mut [Cell], cols: i32, rows: i32) {
    let (mut x, mut y) = (0i32, 0i32);
    let (mut fg, mut bg) = ([220u8; 3], [0u8; 3]);
    let mut i = 0usize;

    while i < data.len() {
        let byte = data[i];
        if byte == 0x1b && i + 1 < data.len() && data[i + 1] == b'[' {
            i += 2;
            let mut nums = [0i32; 16];
            let mut count = 0usize;
            let (mut current, mut have) = (0i32, false);
            while i < data.len() {
                let control = data[i];
                i += 1;
                if control.is_ascii_digit() {
                    current = current * 10 + i32::from(control - b'0');
                    have = true;
                } else if control == b';' {
                    if count < nums.len() {
                        nums[count] = if have { current } else { 0 };
                        count += 1;
                    }
                    (current, have) = (0, false);
                } else {
                    if have && count < nums.len() {
                        nums[count] = current;
                        count += 1;
                    }
                    match control {
                        b'm' => {
                            if count == 0 {
                                count = 1;
                            }
                            let mut index = 0;
                            while index < count {
                                match nums[index] {
                                    0 => {
                                        fg = [220; 3];
                                        bg = [0; 3];
                                    }
                                    38 if index + 4 < count && nums[index + 1] == 2 => {
                                        fg = [
                                            nums[index + 2] as u8,
                                            nums[index + 3] as u8,
                                            nums[index + 4] as u8,
                                        ];
                                        index += 4;
                                    }
                                    48 if index + 4 < count && nums[index + 1] == 2 => {
                                        bg = [
                                            nums[index + 2] as u8,
                                            nums[index + 3] as u8,
                                            nums[index + 4] as u8,
                                        ];
                                        index += 4;
                                    }
                                    _ => {}
                                }
                                index += 1;
                            }
                        }
                        b'H' | b'f' => {
                            y = (if count > 0 { nums[0] } else { 1 } - 1).max(0);
                            x = (if count >= 2 { nums[1] } else { 1 } - 1).max(0);
                        }
                        b'J' => {
                            out.fill(Cell::default());
                            (x, y) = (0, 0);
                        }
                        b'K' if (0..rows).contains(&y) => {
                            let start = y as usize * cols as usize;
                            let from = start + (x.max(0) as usize).min(cols as usize);
                            out[from..start + cols as usize].fill(Cell::default());
                        }
                        _ => {}
                    }
                    break;
                }
            }
            continue;
        }
        match byte {
            b'\n' => {
                y += 1;
                x = 0;
                i += 1;
                continue;
            }
            b'\r' => {
                x = 0;
                i += 1;
                continue;
            }
            0..=0x1f => {
                i += 1;
                continue;
            }
            _ => {}
        }
        let (codepoint, len) = utf8_at(data, i);
        i += len;
        if (0..rows).contains(&y) && (0..cols).contains(&x) {
            out[y as usize * cols as usize + x as usize] = Cell {
                ch: char::from_u32(codepoint).unwrap_or('\u{fffd}'),
                fg,
                bg,
            };
        }
        x += 1;
    }
}

fn utf8_at(data: &[u8], index: usize) -> (u32, usize) {
    let byte = data[index];
    if byte < 0x80 {
        return (u32::from(byte), 1);
    }
    let (need, mut codepoint) = if byte & 0xe0 == 0xc0 {
        (1, u32::from(byte & 0x1f))
    } else if byte & 0xf0 == 0xe0 {
        (2, u32::from(byte & 0x0f))
    } else if byte & 0xf8 == 0xf0 {
        (3, u32::from(byte & 0x07))
    } else {
        return (u32::from(byte), 1);
    };
    let mut len = 1;
    for _ in 0..need {
        if index + len >= data.len() || data[index + len] & 0xc0 != 0x80 {
            return (0xfffd, len);
        }
        codepoint = (codepoint << 6) | u32::from(data[index + len] & 0x3f);
        len += 1;
    }
    (codepoint, len)
}
