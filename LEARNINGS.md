# Learnings from this spike

A weekend prototype to replace Omarchy's screensaver (a fullscreen
terminal running Python `tte`, later Rust `ttfx`) with a native Qt
overlay. Shipped as this repo. These are the things that were not
obvious at the start.

## Check what already shipped

Quattro still opens Ghostty/Alacritty/Foot/Kitty and runs `ttfx` in
it. Idle is a Quickshell oneshot to `omarchy-launch-screensaver`.
There is no native overlay in `basecamp/omarchy`. They *did* already
leave Python (`tte` → `ttfx`, PR #6670). The remaining awkwardness
is the terminal window, not the effect engine.

Look at the live branch before building a replacement. "Native
screensaver" was a reasonable guess and it was wrong.

## The host language follows the toolkit

First pass was "all Rust" because that was the next message after
"native Qt app". Qt is a C++ toolkit. Wrapping it in cxx-qt or
reimplementing the overlay in smithay-client-toolkit is extra
surface. A C++ Qt host with ttfx behind a tiny Rust C ABI is the
low-overhead shape.

When someone says "Qt" and then "Rust is fine", they are usually
talking about the *effects and embed*, not the windowing.

## Do not link a library just because it was named

The brief said "embed libghostty and ttfx". ttfx already produces the
complete VT frame. libghostty-vt added a terminal emulator, Zig build
dependency, and a second Unicode-width policy without changing a pixel.
It was removed.

The resulting path is explicit:

    ttfx → fixed-width VT parser → caller-owned cells → QImage → QPainter

ttfx defines every emitted symbol as one canvas cell. A general terminal
emulator is therefore less correct here: its width policy can reflow the
frame. Ask what a named dependency *does* before it goes in the link line.

## ttfx is a library, not a subprocess

`run_effect` writes DEC cursor save/restore to stdout. That is the
CLI. The embed path is `Effect::build` + `next_frame`. Canvas `0×0`
means "ask the tty", which is `80×24` until a real terminal maps.
Set cols/rows from the layer configure (or `--cols/--rows`).

`recycle_output_string` is `pub(crate)`; the bridge consumes the returned
frame immediately and writes directly into the C++-owned cell buffer.

Effect *names* are the public surface Omarchy uses. Effect-specific
flags (`print --final-gradient-stops`) are a second ABI if anyone
cares.

## Keep the language boundary narrow

The C ABI owns only engine lifetime, options, and "advance into this cell
buffer". C++ owns the persistent buffer and never copies a frame-sized
vector. Rust owns ttfx's types and the fixed-width VT interpretation.

Do not expose an intermediate byte stream, backend enum, or fallback path
unless there are two real implementations. Those surfaces were only
instrumentation for a dependency the final design does not need.

## Drop-in means the name and the argv

Omarchy idle and *System > Screensaver* already call
`omarchy-launch-screensaver`, including positional `force`. App id
`org.omarchy.screensaver` is what Hyprland and `pkill` key off.
A clever new binary name is a failed replacement.

Layer-shell Overlay (exclusive zone −1, exclusive keyboard) is the
actual product change: not an xdg window, so no "must be
Alacritty/Ghostty/Foot/Kitty", and it does not steal focus the way
the launcher's per-monitor `exec` does (that reset the idle timer).

## Spike hygiene

Headless `--frames` + a cheap effect proves the embed without a compositor.
Dense input at a short height catches row-wrap and width-policy regressions.
