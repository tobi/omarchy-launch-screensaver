# omarchy-launch-screensaver

Native Rust/Wayland screensaver for [Omarchy](https://omarchy.org). One executable replaces the terminal-based `omarchy-launch-screensaver` loop without opening Alacritty, Ghostty, Foot, or Kitty.

The binary keeps Omarchy's public contract:

- command: `omarchy-launch-screensaver [force] [options]`
- layer namespace: `org.omarchy.screensaver`
- input: `~/.config/omarchy/branding/screensaver.txt`, then the bundled asset
- toggle: `~/.local/state/omarchy/toggles/screensaver-off`
- one exclusive overlay on every output
- keyboard, pointer motion, click, signal, or surface close dismisses the overlay
- ttfx simulation defaults to 120 Hz; Wayland presentation is capped at 30 Hz

## Architecture

```text
branding text
    │
    ▼
ttfx Effect + EngineCtx (Rust, in process, 120 Hz)
    │ complete VT frame
    ▼
fixed-width VT parser → caller-owned Cell grid
    │
    ▼
fontdue glyph cache + ARGB8888 software rasterizer
    │ only when cells, fade, scale, or geometry change
    ▼
smithay-client-toolkit → wl_shm buffer → wlr-layer-shell surface per output
```

There is no Qt, C++, C ABI, terminal emulator, terminal child, generated Wayland C code, or shell wrapper. `ttfx` is linked as a Rust crate and driven through `Effect::next_frame`. The small VT parser handles only the complete-frame cursor and true-color sequences ttfx emits; it is not a general terminal emulator.

Rendering is invalidation-driven:

1. ttfx advances at its requested simulation rate.
2. A hash of the caller-owned cell grid detects unchanged frames without copying it.
3. Fade opacity is quantized to the output alpha byte.
4. Dirty surfaces are eligible at 30 Hz.
5. A `wl_surface.frame` callback must release presentation pacing.
6. `SlotPool::canvas` must confirm the persistent SHM buffer is no longer owned by the compositor.
7. During fades, the full buffer is redrawn because every pixel's alpha changed.
8. Once opaque, the current cells are compared with each output's last-presented snapshot; only the exact changed-cell bounding box is cleared, redrawn, damaged, attached, and committed.

Each output has one reusable SHM buffer and one small cell snapshot for incremental damage. HiDPI outputs render at their compositor scale. Glyph bitmaps are cached by `(character, scale)` and clipped to terminal-cell bounds, making partial and full rasterization byte-identical.

## Build and install

Requirements: Rust/Cargo and a Wayland compositor implementing `wlr-layer-shell` (Hyprland does).

```bash
make
make check
make install PREFIX="$HOME/.local"
```

Equivalent Cargo command:

```bash
CARGO_TARGET_DIR=build/rust cargo build --release
```

The executable lands at `build/rust/release/omarchy-launch-screensaver`.

## Run

```bash
omarchy-launch-screensaver force
omarchy-launch-screensaver force --effect print --seed 1
omarchy-launch-screensaver --headless --frames 30 --effect decrypt --cols 80 --rows 24
```

Important options:

- `force`, `--force`: ignore the screensaver-off toggle and lock-state guard
- `--effect NAME`: pin an effect; omission picks randomly
- `--include-effects LIST`, `--exclude-effects LIST`: filter random selection
- `--frame-rate N`: ttfx simulation rate; default 120
- `--fade IN[,OUT]`: fade durations in milliseconds; defaults 1000 and 200
- `--seed N`: deterministic effect selection and animation
- `--headless --frames N --cols N --rows N`: deterministic non-Wayland contract mode

Run `omarchy-launch-screensaver --help` for the full list.

## Deterministic benchmark

```bash
make benchmark
```

The benchmark runs 480 seeded `decrypt` simulation steps and 120 presentation frames against a persistent 5120×2880 ARGB buffer. It warms the font/glyph caches, counts allocator traffic, verifies incremental output byte-for-byte against a full-frame oracle, and emits an FNV-1a checksum.

Measured on the workstation described by the repository session:

| renderer | total | per frame | throughput | checksum |
|---|---:|---:|---:|---|
| full-buffer baseline | 379.1 ms | 3.159 ms | 316.5 fps | `12301096be43a75d` |
| incremental damage | 22.7 ms | 0.189 ms | 5,278.3 fps | `2e1bd28d1bcdb090` |

The checksum changed when glyph output was deliberately clipped to terminal-cell bounds; the benchmark's full-frame oracle has the same new checksum. The measured loop performs 2,749 allocations totaling 14,371,148 bytes across 480 ttfx steps; those counts are deterministic and unchanged by the damage optimization. Wall-clock numbers vary with machine load.

## Tests

```bash
CARGO_TARGET_DIR=build/rust cargo test --all-targets
```

The suite checks CLI compatibility, deterministic seeded rendering, non-empty cell output, and operation with closed standard streams. The headless summary includes `backend=rust`; no compositor is needed.

## Credits

- [ttfx](https://github.com/omacom-io/ttfx) — Rust port of TerminalTextEffects
- [Smithay Client Toolkit](https://github.com/Smithay/client-toolkit) — Wayland client primitives
- [fontdue](https://github.com/mooman219/fontdue) — software glyph rasterization

MIT; see [LICENSE](LICENSE).
