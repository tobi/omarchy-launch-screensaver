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
surface. C++ Qt host, Rust/Zig as libraries, was the low-overhead
shape.

When someone says "Qt" and then "Rust is fine", they are usually
talking about the *effects and embed*, not the windowing.

## Do not link a library just because it was named

The brief said "embed libghostty and ttfx". ttfx already has a cell
grid (`next_frame` / `render_cells`). libghostty-vt is a VT parser
and render-state, not a GPU widget. The pipeline became:

    ttfx (already cells) → ANSI string → libghostty-vt → cells → QPainter

That middle hop does not change the picture. Ghostty's real renderer
is not in the C API yet. We linked it because it was asked for, not
because the screensaver needed it.

Ask what the dependency *does* before it goes in the link line.
Painting ttfx's canvas straight to the shm/QImage is enough.

## ttfx is a library, not a subprocess

`run_effect` writes DEC cursor save/restore to stdout. That is the
CLI. The embed path is `Effect::build` + `next_frame`. Canvas `0×0`
means "ask the tty", which is `80×24` until a real terminal maps.
Set cols/rows from the layer configure (or `--cols/--rows`).

`recycle_output_string` is `pub(crate)`; a cdylib cannot call it.
Keep the last frame or expose a tiny C ABI.

Effect *names* are the public surface Omarchy uses. Effect-specific
flags (`print --final-gradient-stops`) are a second ABI if anyone
cares.

## C at the language boundary

C++ → Zig's `libghostty-vt` C API is the short path. The Rust
`libghostty-vt` crate is the same .a with more wrappers. Do not do
C++ → crate → sys → Zig for a Qt host.

Ghostty headers use `typedef struct Foo *Foo`, which is illegal in
C++. wlr-layer-shell's scanner emits a parameter named `namespace`.
Keep those calls in `.c` files. Do not "fix" the headers.

Ghostty **v1.3.1** `-Demit-lib-vt` is OSC/SGR/key encode only. The
`GhosttyTerminal` / `vt_write` / render-state API is on **main** and
wants **Zig 0.16**. Pin that in the README or you will spend an hour
on a library that cannot parse a frame.

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

Headless `--frames` + a cheap effect is how you prove the embed
without a compositor. `backend=libghostty` in that output is the
only evidence the C API actually linked.

Do not vendor Ghostty. It is large and moves. Fetch at build time.

Credit ChrisBuilds / ttfx / Ghostty in the tree. The art is theirs.
