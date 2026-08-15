# third_party

This directory is **not** a Ghostty vendor tree. Full Ghostty checkouts and
`libghostty-vt` prefixes are gitignored — clone and build them locally.

## libghostty-vt (C API)

The **full** terminal C API (`ghostty_terminal_new`,
`ghostty_terminal_vt_write`, render-state iterators) landed after Ghostty
v1.3.1. v1.3.1's `lib-vt` is only OSC/SGR/key encode. Build **ghostty main**
with **Zig 0.16**:

```
git clone --depth 1 https://github.com/ghostty-org/ghostty.git third_party/ghostty-main
cd third_party/ghostty-main
zig build -Demit-lib-vt -Doptimize=ReleaseFast --prefix ../ghostty-prefix-main
```

CMake enables `HAVE_LIBGHOSTTY` when it finds
`third_party/ghostty-prefix-main/include/ghostty/vt/terminal.h` next to
`lib/libghostty-vt.so` (or `.a`). A `ghostty-prefix` sibling is also accepted.

Headers are upstream C (`ghostty/vt.h`). We do **not** go
C++ → Rust bindgen → libghostty-vt-sys. `src/ghostty_host.c` is a thin C
wrapper so the C++ host never includes those headers (C++ rejects
`typedef struct Foo *Foo`).

If the Zig build is missing, the pipeline still compiles with a thin ANSI
fallback. Ghostty call sites stay wired behind `HAVE_LIBGHOSTTY`.

## wlr-layer-shell-unstable-v1.xml

From [swaywm/wlr-protocols](https://github.com/swaywm/wlr-protocols). CMake
runs `wayland-scanner`. Overlay layer, exclusive zone −1, exclusive keyboard.
Compiled as C (`src/layer_shell.c`) because the generated header uses a
parameter named `namespace`.
