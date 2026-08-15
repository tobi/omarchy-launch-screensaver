#include "layer_shell.h"

#ifdef HAVE_LAYER_SHELL
#include <string.h>
#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

struct bind_state {
    struct zwlr_layer_shell_v1 *shell;
};

static void registry_global(void *data, struct wl_registry *reg, uint32_t name,
                            const char *iface, uint32_t ver) {
    struct bind_state *b = data;
    if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
        uint32_t v = ver < 4 ? ver : 4;
        b->shell = wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface, v);
    }
}

static void registry_remove(void *data, struct wl_registry *reg, uint32_t name) {
    (void)data;
    (void)reg;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_remove,
};

int ssaver_apply_layer_shell(void *wl_display, void *wl_surface, void *wl_output) {
    struct wl_display *display = wl_display;
    struct wl_surface *surface = wl_surface;
    if (!display || !surface)
        return 0;

    struct bind_state bind = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &bind);
    wl_display_roundtrip(display);
    if (!bind.shell)
        return 0;

    struct zwlr_layer_surface_v1 *ls = zwlr_layer_shell_v1_get_layer_surface(
        bind.shell, surface, wl_output, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        "org.omarchy.screensaver");
    zwlr_layer_surface_v1_set_anchor(ls, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                             ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                             ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                             ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(ls, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        ls, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    zwlr_layer_surface_v1_set_size(ls, 0, 0);
    wl_surface_commit(surface);
    wl_display_roundtrip(display);
    return 1;
}
#else
int ssaver_apply_layer_shell(void *wl_display, void *wl_surface, void *wl_output) {
    (void)wl_display;
    (void)wl_surface;
    (void)wl_output;
    return 0;
}
#endif
