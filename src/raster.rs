use std::{collections::HashMap, env, fs, path::PathBuf, process::Command};

use anyhow::{Context, Result, anyhow};
use fontdue::{Font, FontSettings, Metrics};

use crate::vt::Cell;

const FONT_PX: f32 = 24.0; // Qt's 18 pt terminal font at the standard 96 DPI.

struct Glyph {
    metrics: Metrics,
    coverage: Vec<u8>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Damage {
    pub x: i32,
    pub y: i32,
    pub width: i32,
    pub height: i32,
}

pub struct Rasterizer {
    font: Font,
    glyphs: HashMap<(char, i32), Glyph>,
    cell_width: i32,
    cell_height: i32,
    ascent: i32,
}

impl Rasterizer {
    pub fn load() -> Result<Self> {
        let path = resolve_monospace_font()?;
        let data = fs::read(&path)
            .with_context(|| format!("cannot read monospace font {}", path.display()))?;
        let font = Font::from_bytes(
            data,
            FontSettings {
                scale: FONT_PX,
                load_substitutions: false,
                ..FontSettings::default()
            },
        )
        .map_err(|error| anyhow!("cannot parse monospace font {}: {error}", path.display()))?;
        let line = font
            .horizontal_line_metrics(FONT_PX)
            .context("selected font has no horizontal metrics")?;
        let cell_width = font.metrics('M', FONT_PX).advance_width.round().max(1.0) as i32;
        let cell_height = line.new_line_size.ceil().max(1.0) as i32;
        let ascent = line.ascent.ceil() as i32;
        Ok(Self {
            font,
            glyphs: HashMap::new(),
            cell_width,
            cell_height,
            ascent,
        })
    }

    pub fn grid_size(&self, width: i32, height: i32, scale: i32) -> (i32, i32) {
        let scale = scale.max(1);
        (
            (width / (self.cell_width * scale)).max(1),
            (height / (self.cell_height * scale)).max(1),
        )
    }

    #[allow(clippy::too_many_arguments)]
    pub fn render(
        &mut self,
        pixels: &mut [u8],
        width: i32,
        height: i32,
        cells: &[Cell],
        cols: i32,
        rows: i32,
        opacity: f32,
        scale: i32,
    ) -> Result<()> {
        validate_target(pixels, width, height, cells, cols, rows)?;
        let alpha = (opacity.clamp(0.0, 1.0) * 255.0).round() as u8;
        fill_rect(pixels, width, height, 0, 0, width, height, [0; 3], alpha);
        self.draw_region(
            pixels, width, height, cells, cols, rows, alpha, scale, 0, cols, 0, rows,
        );
        Ok(())
    }

    #[allow(clippy::too_many_arguments)]
    pub fn render_delta(
        &mut self,
        pixels: &mut [u8],
        width: i32,
        height: i32,
        cells: &[Cell],
        previous: &[Cell],
        cols: i32,
        rows: i32,
        scale: i32,
    ) -> Result<Option<Damage>> {
        validate_target(pixels, width, height, cells, cols, rows)?;
        let count = cols.max(0) as usize * rows.max(0) as usize;
        if previous.len() < count {
            return Err(anyhow!("previous cell buffer is smaller than the frame"));
        }
        let (mut min_col, mut min_row) = (cols, rows);
        let (mut max_col, mut max_row) = (-1, -1);
        for row in 0..rows {
            for col in 0..cols {
                let index = row as usize * cols as usize + col as usize;
                if cells[index] != previous[index] {
                    min_col = min_col.min(col);
                    min_row = min_row.min(row);
                    max_col = max_col.max(col);
                    max_row = max_row.max(row);
                }
            }
        }
        if max_col < 0 {
            return Ok(None);
        }
        // Glyphs are clipped to their terminal cell, so the changed-cell
        // bounding box is sufficient and exactly reproducible.
        let col_start = min_col;
        let col_end = max_col + 1;
        let row_start = min_row;
        let row_end = max_row + 1;
        let scale = scale.max(1);
        let cell_width = self.cell_width * scale;
        let cell_height = self.cell_height * scale;
        let origin_x = (width - cols * cell_width) / 2;
        let origin_y = (height - rows * cell_height) / 2;
        let x0 = (origin_x + col_start * cell_width).clamp(0, width);
        let y0 = (origin_y + row_start * cell_height).clamp(0, height);
        let x1 = (origin_x + col_end * cell_width).clamp(0, width);
        let y1 = (origin_y + row_end * cell_height).clamp(0, height);
        if x1 <= x0 || y1 <= y0 {
            return Ok(None);
        }
        fill_rect(pixels, width, height, x0, y0, x1 - x0, y1 - y0, [0; 3], 255);
        self.draw_region(
            pixels, width, height, cells, cols, rows, 255, scale, col_start, col_end, row_start,
            row_end,
        );
        Ok(Some(Damage {
            x: x0,
            y: y0,
            width: x1 - x0,
            height: y1 - y0,
        }))
    }

    #[allow(clippy::too_many_arguments)]
    fn draw_region(
        &mut self,
        pixels: &mut [u8],
        width: i32,
        height: i32,
        cells: &[Cell],
        cols: i32,
        rows: i32,
        alpha: u8,
        scale: i32,
        col_start: i32,
        col_end: i32,
        row_start: i32,
        row_end: i32,
    ) {
        let scale = scale.max(1);
        let cell_width = self.cell_width * scale;
        let cell_height = self.cell_height * scale;
        let origin_x = (width - cols * cell_width) / 2;
        let origin_y = (height - rows * cell_height) / 2;
        for row in row_start..row_end {
            for col in col_start..col_end {
                let cell = cells[row as usize * cols as usize + col as usize];
                let x = origin_x + col * cell_width;
                let y = origin_y + row * cell_height;
                if cell.bg != [0; 3] {
                    fill_rect(
                        pixels,
                        width,
                        height,
                        x,
                        y,
                        cell_width,
                        cell_height,
                        cell.bg,
                        alpha,
                    );
                }
                if cell.ch == ' ' || cell.ch == '\0' {
                    continue;
                }
                let key = (cell.ch, scale);
                let font = &self.font;
                let glyph = self.glyphs.entry(key).or_insert_with(|| {
                    let (metrics, coverage) = font.rasterize(cell.ch, FONT_PX * scale as f32);
                    Glyph { metrics, coverage }
                });
                let glyph_x = x + glyph.metrics.xmin;
                let baseline = y + self.ascent * scale;
                let glyph_y = baseline - glyph.metrics.ymin - glyph.metrics.height as i32;
                draw_glyph(
                    pixels,
                    width,
                    height,
                    glyph_x,
                    glyph_y,
                    x,
                    y,
                    cell_width,
                    cell_height,
                    glyph,
                    cell.fg,
                    cell.bg,
                    alpha,
                );
            }
        }
    }
}
fn resolve_monospace_font() -> Result<PathBuf> {
    let mut candidates = vec![PathBuf::from(
        "/usr/share/fonts/TTF/JetBrainsMonoNerdFont-Regular.ttf",
    )];
    if let Some(home) = env::var_os("HOME").map(PathBuf::from) {
        candidates.push(home.join(".local/share/fonts/JetBrainsMonoNerdFont-Regular.ttf"));
        candidates.push(home.join(".fonts/JetBrainsMonoNerdFont-Regular.ttf"));
    }
    if let Some(path) = candidates.into_iter().find(|path| path.is_file()) {
        return Ok(path);
    }
    let output = Command::new("fc-match")
        .args([
            "-f",
            "%{file}\n",
            "JetBrainsMono Nerd Font,JetBrains Mono,monospace:style=Regular",
        ])
        .output()
        .context("cannot locate a monospace font: fc-match failed")?;
    let path = PathBuf::from(
        String::from_utf8(output.stdout)
            .context("fc-match returned a non-UTF-8 font path")?
            .lines()
            .next()
            .unwrap_or_default(),
    );
    if !output.status.success() || !path.is_file() {
        return Err(anyhow!("fc-match did not return a readable monospace font"));
    }
    Ok(path)
}

fn validate_target(
    pixels: &[u8],
    width: i32,
    height: i32,
    cells: &[Cell],
    cols: i32,
    rows: i32,
) -> Result<()> {
    let pixel_count = width.max(0) as usize * height.max(0) as usize * 4;
    let cell_count = cols.max(0) as usize * rows.max(0) as usize;
    if pixels.len() < pixel_count || cells.len() < cell_count {
        return Err(anyhow!("raster target is smaller than the frame"));
    }
    Ok(())
}

#[allow(clippy::too_many_arguments)]
fn fill_rect(
    pixels: &mut [u8],
    width: i32,
    height: i32,
    x: i32,
    y: i32,
    rect_width: i32,
    rect_height: i32,
    rgb: [u8; 3],
    alpha: u8,
) {
    let x0 = x.clamp(0, width);
    let y0 = y.clamp(0, height);
    let x1 = (x + rect_width).clamp(0, width);
    let y1 = (y + rect_height).clamp(0, height);
    let premultiplied = [
        multiply(rgb[2], alpha),
        multiply(rgb[1], alpha),
        multiply(rgb[0], alpha),
        alpha,
    ];
    let fill = |bytes: &mut [u8]| {
        // Vec and wl_shm storage are word-aligned; keep a byte fallback for the
        // generic slice contract.
        let (prefix, words, suffix) = unsafe { bytes.align_to_mut::<u32>() };
        if prefix.is_empty() && suffix.is_empty() {
            words.fill(u32::from_ne_bytes(premultiplied));
        } else {
            let (pixels, remainder) = bytes.as_chunks_mut::<4>();
            debug_assert!(remainder.is_empty());
            pixels.fill(premultiplied);
        }
    };
    if x0 == 0 && y0 == 0 && x1 == width && y1 == height {
        fill(&mut pixels[..width as usize * height as usize * 4]);
        return;
    }
    for row in y0..y1 {
        let start = (row as usize * width as usize + x0 as usize) * 4;
        let end = (row as usize * width as usize + x1 as usize) * 4;
        fill(&mut pixels[start..end]);
    }
}

#[allow(clippy::too_many_arguments)]
fn draw_glyph(
    pixels: &mut [u8],
    width: i32,
    height: i32,
    x: i32,
    y: i32,
    clip_x: i32,
    clip_y: i32,
    clip_width: i32,
    clip_height: i32,
    glyph: &Glyph,
    foreground: [u8; 3],
    background: [u8; 3],
    alpha: u8,
) {
    for glyph_y in 0..glyph.metrics.height as i32 {
        let target_y = y + glyph_y;
        if !(0..height).contains(&target_y) || !(clip_y..clip_y + clip_height).contains(&target_y) {
            continue;
        }
        for glyph_x in 0..glyph.metrics.width as i32 {
            let target_x = x + glyph_x;
            if !(0..width).contains(&target_x) || !(clip_x..clip_x + clip_width).contains(&target_x)
            {
                continue;
            }
            let coverage =
                glyph.coverage[glyph_y as usize * glyph.metrics.width + glyph_x as usize];
            if coverage == 0 {
                continue;
            }
            let mix = |foreground: u8, background: u8| {
                let value = u16::from(foreground) * u16::from(coverage)
                    + u16::from(background) * u16::from(255 - coverage);
                (value / 255) as u8
            };
            let rgb = [
                mix(foreground[0], background[0]),
                mix(foreground[1], background[1]),
                mix(foreground[2], background[2]),
            ];
            let offset = (target_y as usize * width as usize + target_x as usize) * 4;
            pixels[offset..offset + 4].copy_from_slice(&[
                multiply(rgb[2], alpha),
                multiply(rgb[1], alpha),
                multiply(rgb[0], alpha),
                alpha,
            ]);
        }
    }
}

fn multiply(color: u8, alpha: u8) -> u8 {
    (u16::from(color) * u16::from(alpha) / 255) as u8
}
