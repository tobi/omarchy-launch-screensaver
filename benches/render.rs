use std::{
    alloc::{GlobalAlloc, Layout, System},
    hint::black_box,
    sync::atomic::{AtomicU64, Ordering},
    time::Instant,
};

use omarchy_launch_screensaver::{engine::Animation, options::Options, raster::Rasterizer};

const WIDTH: i32 = 2560;
const HEIGHT: i32 = 1440;
const SCALE: i32 = 2;
const PRESENTED_FRAMES: usize = 120;
const ENGINE_STEPS_PER_PRESENT: usize = 4;
struct CountingAllocator;

static ALLOCATIONS: AtomicU64 = AtomicU64::new(0);
static DEALLOCATIONS: AtomicU64 = AtomicU64::new(0);
static ALLOCATED_BYTES: AtomicU64 = AtomicU64::new(0);

unsafe impl GlobalAlloc for CountingAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let pointer = unsafe { System.alloc(layout) };
        if !pointer.is_null() {
            ALLOCATIONS.fetch_add(1, Ordering::Relaxed);
            ALLOCATED_BYTES.fetch_add(layout.size() as u64, Ordering::Relaxed);
        }
        pointer
    }

    unsafe fn dealloc(&self, pointer: *mut u8, layout: Layout) {
        DEALLOCATIONS.fetch_add(1, Ordering::Relaxed);
        unsafe { System.dealloc(pointer, layout) };
    }

    unsafe fn realloc(&self, pointer: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
        let replacement = unsafe { System.realloc(pointer, layout, new_size) };
        if !replacement.is_null() {
            ALLOCATIONS.fetch_add(1, Ordering::Relaxed);
            ALLOCATED_BYTES.fetch_add(new_size as u64, Ordering::Relaxed);
        }
        replacement
    }
}

#[global_allocator]
static ALLOCATOR: CountingAllocator = CountingAllocator;

fn main() {
    let mut rasterizer = Rasterizer::load().expect("load monospace font");
    let physical_width = WIDTH * SCALE;
    let physical_height = HEIGHT * SCALE;
    let (cols, rows) = rasterizer.grid_size(physical_width, physical_height, SCALE);
    let options = Options {
        effect: Some("decrypt".into()),
        seed: Some(1),
        ..Options::default()
    };
    let mut animation = Animation::new(
        include_str!("../assets/screensaver.txt").to_owned(),
        options,
        cols,
        rows,
    )
    .expect("create animation");
    let mut pixels = vec![0u8; physical_width as usize * physical_height as usize * 4];
    let mut previous_cells = Vec::new();

    // Populate effect state and the glyph cache before measuring steady-state work.
    advance_and_render(
        &mut animation,
        &mut rasterizer,
        &mut pixels,
        &mut previous_cells,
        physical_width,
        physical_height,
        cols,
        rows,
        8,
    );

    let allocations_before = ALLOCATIONS.load(Ordering::Relaxed);
    let deallocations_before = DEALLOCATIONS.load(Ordering::Relaxed);
    let bytes_before = ALLOCATED_BYTES.load(Ordering::Relaxed);
    let started = Instant::now();
    advance_and_render(
        &mut animation,
        &mut rasterizer,
        &mut pixels,
        &mut previous_cells,
        physical_width,
        physical_height,
        cols,
        rows,
        PRESENTED_FRAMES,
    );
    let elapsed = started.elapsed();
    let allocations = ALLOCATIONS.load(Ordering::Relaxed) - allocations_before;
    let deallocations = DEALLOCATIONS.load(Ordering::Relaxed) - deallocations_before;
    let allocated_bytes = ALLOCATED_BYTES.load(Ordering::Relaxed) - bytes_before;
    let mut oracle = vec![0u8; pixels.len()];
    rasterizer
        .render(
            &mut oracle,
            physical_width,
            physical_height,
            animation.cells(),
            cols,
            rows,
            1.0,
            SCALE,
        )
        .expect("render full-frame oracle");
    assert_eq!(
        pixels, oracle,
        "incremental raster diverged from full-frame raster"
    );
    black_box(&pixels);
    let checksum = fnv1a(&pixels);
    println!(
        "render-bench frames={PRESENTED_FRAMES} size={}x{} scale={SCALE} cells={cols}x{rows} elapsed_ms={:.3} ms_per_frame={:.3} fps={:.1} allocations={allocations} deallocations={deallocations} allocated_bytes={allocated_bytes} checksum={checksum:016x}",
        physical_width,
        physical_height,
        elapsed.as_secs_f64() * 1000.0,
        elapsed.as_secs_f64() * 1000.0 / PRESENTED_FRAMES as f64,
        PRESENTED_FRAMES as f64 / elapsed.as_secs_f64(),
    );
}

#[allow(clippy::too_many_arguments)]
fn advance_and_render(
    animation: &mut Animation,
    rasterizer: &mut Rasterizer,
    pixels: &mut [u8],
    previous_cells: &mut Vec<omarchy_launch_screensaver::vt::Cell>,
    width: i32,
    height: i32,
    cols: i32,
    rows: i32,
    frames: usize,
) {
    for _ in 0..frames {
        for _ in 0..ENGINE_STEPS_PER_PRESENT {
            animation.advance().expect("advance ttfx");
        }
        let current = animation.cells();
        if previous_cells.len() == current.len() {
            rasterizer
                .render_delta(
                    pixels,
                    width,
                    height,
                    current,
                    previous_cells,
                    cols,
                    rows,
                    SCALE,
                )
                .expect("raster frame");
        } else {
            rasterizer
                .render(pixels, width, height, current, cols, rows, 1.0, SCALE)
                .expect("raster frame");
            previous_cells.resize(current.len(), Default::default());
        }
        previous_cells.copy_from_slice(current);
    }
}

fn fnv1a(bytes: &[u8]) -> u64 {
    bytes.iter().fold(0xcbf29ce484222325u64, |hash, byte| {
        (hash ^ u64::from(*byte)).wrapping_mul(0x100000001b3)
    })
}
