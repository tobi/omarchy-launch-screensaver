# omarchy-launch-screensaver

Native **Qt / Wayland** screensaver for [Omarchy](https://omarchy.org). One
binary replaces both `omarchy-launch-screensaver` and `omarchy-screensaver`
(the terminal + `tte`/`ttfx` loop). A real `zwlr_layer_shell_v1` overlay —
not a terminal running `ttfx`.

```
git clone https://github.com/tobi/omarchy-launch-screensaver.git
cd omarchy-launch-screensaver
```

```
C++ Qt host (window / input / paint)
  └─ rust/ttfx_c   ttfx + libghostty-vt crate, C ABI
```

The built binary is named **`omarchy-launch-screensaver`**. App id / layer
namespace: **`org.omarchy.screensaver`** so Hyprland rules and
`pkill -f '[o]rg.omarchy.screensaver'` still match.

## Credits

- **[ttfx](https://github.com/omacom-io/ttfx)** (MIT) — Rust port of
  [ChrisBuilds/terminaltexteffects](https://github.com/ChrisBuilds/terminaltexteffects)
  (MIT). Linked in-process via `rust/ttfx_c`.
- **[libghostty-vt](https://crates.io/crates/libghostty-vt)** (MIT) —
  Ghostty VT parse + cell render-state, via the Rust crate (no local Zig
  checkout). `libghostty-vt-sys` 0.2.1 needs **Zig 0.15.2** on `PATH`.

This project is MIT (see `LICENSE`).

## Why

Omarchy today opens Alacritty/Ghostty/Foot/Kitty and runs:

```
ttfx -i ~/.config/omarchy/branding/screensaver.txt \
  --frame-rate 120 --canvas-width 0 --canvas-height 0 --reuse-canvas \
  --anchor-canvas c --anchor-text c \
  --random-effect --no-eol --no-restore-cursor
```

That is a real window. This is a `zwlr_layer_shell_v1` **Overlay** (exclusive
zone −1, exclusive keyboard) per output. It fades in/out and quits on key,
pointer motion, or click. It does **not** spawn a terminal or a `ttfx`/`tte`
child, and it does not require Alacritty/Ghostty/Foot/Kitty.

## Drop-in install

Idle (Omarchy shell / hypridle) and the menu entry *System > Screensaver*
already call `omarchy-launch-screensaver`. Point that name at this binary:

```
cmake --install build --prefix /usr/local
# or: install -m755 build/omarchy-launch-screensaver /usr/local/bin/
```

If Omarchy's script is still first on `PATH`, replace it or put `/usr/local/bin`
ahead of it. The menu uses `omarchy-launch-screensaver force`.

## Behaviour (matches the quattro scripts)

- Exit 0 if a screensaver is already running (pidfile lock + cmdline
  `org.omarchy.screensaver`).
- If the screensaver toggle is off (`omarchy-toggle-enabled screensaver-off`,
  i.e. `~/.local/state/omarchy/toggles/screensaver-off`) **and** argv is not
  `force` / `--force`, exit 1.
- `force` still launches when the toggle is off.
- Idle path: if `omarchy-shell lock isLocked` is true, do not start. `force`
  still starts (the launch script never checks the lock).
- Quiet Walker if `walker` exists (`walker -q`). Missing walker is not an error
  (quattro dropped Walker; master still calls this).
- One overlay per monitor/output.
- Hide the cursor (`BlankCursor` + `hyprctl` `cursor:invisible` when present);
  restore on exit.
- Black background. When an effect finishes, loop another random effect
  (or the pinned `--effect`).
- Any key, pointer motion, or click → fade out → exit 0. Also quit if exclusive
  keyboard is lost.

## CLI

```
omarchy-launch-screensaver [force] [options]

  force, --force          launch even if screensaver toggle is off
  -h, --help
  -i, --input PATH        also --input-file
  --effect NAME
  -R, --random-effect     default
  --include-effects LIST
  --exclude-effects LIST
  --frame-rate N          default 120
  --canvas-width N        0 = output width
  --canvas-height N       0 = output height
  --reuse-canvas
  --anchor-canvas c|...
  --anchor-text c|...
  --no-eol
  --no-restore-cursor
  --seed N
  --headless              no Wayland; for tests
  --frames N              headless only
  --cols N --rows N       headless canvas
```

Positional `force` is required for Omarchy's menu path.

Input default: `~/.config/omarchy/branding/screensaver.txt`, then bundled
`assets/screensaver.txt`.

`--headless` is how this box / CI proves the embed (no Wayland required).
Canvas size comes from `--cols/--rows` or the layer configure — never
`terminal_size()`'s 80×24.

## How to run

```
# tests / no compositor
./build/omarchy-launch-screensaver --headless --frames 20 --effect wipe --cols 40 --rows 12

# on a running Omarchy / Hyprland session
./build/omarchy-launch-screensaver --effect matrix
./build/omarchy-launch-screensaver force
./build/omarchy-launch-screensaver --input ~/.config/omarchy/branding/screensaver.txt
```

## Toggle

`omarchy toggle screensaver` writes `screensaver-off`. Idle will not start
this binary until you toggle it back (or pass `force`).

## How to build

Needs: CMake, Ninja, g++, Qt6 (Core/Gui + Wayland), Rust (`cargo` on `PATH`),
**Zig 0.15.2** (pulled in by `libghostty-vt-sys`; `mise install zig@0.15.2`).

```
make
make run
ctest --test-dir build --output-on-failure
```

The binary lands at `build/omarchy-launch-screensaver`. CMake runs
`cargo build --release` on `rust/ttfx_c` and links `libttfx_c.so`.
`ttfx` comes from `https://github.com/omacom-io/ttfx`.

## Status

- Software QPainter blit of cells. Fade via `QWindow::setOpacity` (and
  pixel alpha if the compositor ignores it). `--fade N` (default 500);
  dismiss uses N/2.
- One ttfx engine per output (Omarchy launched one `ttfx` per monitor).
- Effect-specific CLI flags are not wired (every effect *name* works).
- `ttfx::recycle_output_string` is `pub(crate)`; the cdylib keeps the last
  frame instead.
- VT raster is the `libghostty-vt` crate. ANSI fallback remains if raster
  returns 0.
