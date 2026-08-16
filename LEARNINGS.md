# Implementation notes

## Preserve ttfx semantics, not terminal machinery

`ttfx` already exposes `Effect::build` and `Effect::next_frame`. Calling those APIs directly preserves effect selection, seeded RNG, anchors, canvas sizing, and the 120 Hz virtual clock. A terminal process, PTY, libghostty, or a terminal state machine would only reinterpret a complete frame that ttfx already computed.

The remaining VT parser is intentionally narrow: cursor positioning, erase, line movement, UTF-8 symbols, and true-color SGR. It places every emitted symbol in one cell because that is ttfx's layout invariant. Applying a general terminal Unicode-width table would change effect geometry.

## Separate simulation from presentation

`--frame-rate` is an engine clock, not a monitor refresh cap. Lowering it to 30 slows clock-driven effects. The application therefore advances ttfx at 120 Hz by default and presents the newest frame at no more than 30 Hz.

Presentation has three independent gates:

- content or fade opacity actually changed;
- the 30 Hz deadline passed;
- the compositor sent the previous `wl_surface.frame` callback and released the SHM slot.

This keeps effect timing identical while avoiding redundant rasterization and commits.

## Let Wayland own display geometry

One `wlr-layer-shell` overlay is bound to each `wl_output`. All edges are anchored, size is compositor-selected, layer is `Overlay`, exclusive zone is `-1`, and keyboard interactivity is `Exclusive`. Configure and scale events are the only geometry authority.

The largest configured output determines the shared ttfx cell grid. Every output renders that same frame centered in its own buffer. Buffer scale controls physical raster dimensions, so an 18 pt-equivalent font remains the same logical size on HiDPI outputs.

## Keep hot-path ownership explicit

The animation owns one mutable cell vector. The VT parser fills it in place. Each output owns one persistent SHM buffer and one last-presented cell snapshot. The snapshot costs roughly 96 KiB at the benchmark's 182×45 grid but avoids rewriting a 56.25 MiB 5K buffer when only a small cell region changed. The rasterizer owns a glyph bitmap cache keyed by character and scale; no frame-sized image copy sits between stages.

A stable cell hash rejects completely unchanged engine output before per-output comparison. Alpha is compared after conversion to the actual 8-bit surface value. During the fade, alpha changes require a full redraw. Once opaque, clipped cell glyphs make incremental redraw byte-identical to a full-frame oracle. Rasterization occurs only when a surface can immediately attach and commit its buffer.

## Signals are part of dismissal

SIGINT, SIGTERM, SIGHUP, and SIGQUIT set an atomic dismissal flag. The normal event loop performs the configured fade-out, drops Wayland resources, and restores Hyprland cursor visibility. An abrupt default signal handler would bypass the cursor guard.
