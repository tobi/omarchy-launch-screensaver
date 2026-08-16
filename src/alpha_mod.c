#include "alpha_mod.h"

#ifdef HAVE_ALPHA_MOD
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include "alpha-modifier-v1-client-protocol.h"

struct alpha {
    struct wp_alpha_modifier_v1 *mod;
    struct wp_alpha_modifier_surface_v1 *surf;
};

static void registry_global(void *data, struct wl_registry *reg, uint32_t name,
                            const char *iface, uint32_t ver) {
    struct alpha *a = data;
    if (strcmp(iface, wp_alpha_modifier_v1_interface.name) == 0) {
        uint32_t v = ver < 1 ? ver : 1;
        a->mod = wl_registry_bind(reg, name, &wp_alpha_modifier_v1_interface, v);
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

void *ssaver_alpha_attach(void *wl_display, void *wl_surface) {
    struct wl_display *display = wl_display;
    struct wl_surface *surface = wl_surface;
    if (!display || !surface)
        return NULL;

    struct alpha *a = calloc(1, sizeof(*a));
    if (!a)
        return NULL;
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, a);
    wl_display_roundtrip(display);
    wl_registry_destroy(registry);
    if (!a->mod) {
        free(a);
        return NULL;
    }
    a->surf = wp_alpha_modifier_v1_get_surface(a->mod, surface);
    if (!a->surf) {
        wp_alpha_modifier_v1_destroy(a->mod);
        free(a);
        return NULL;
    }
    return a;
}

void ssaver_alpha_set(void *handle, float factor) {
    struct alpha *a = handle;
    if (!a || !a->surf)
        return;
    if (factor < 0.f)
        factor = 0.f;
    if (factor > 1.f)
        factor = 1.f;
    uint32_t mul = (uint32_t)(factor * (float)UINT32_MAX + 0.5f);
    wp_alpha_modifier_surface_v1_set_multiplier(a->surf, mul);
}

void ssaver_alpha_destroy(void *handle) {
    struct alpha *a = handle;
    if (!a)
        return;
    if (a->surf)
        wp_alpha_modifier_surface_v1_destroy(a->surf);
    if (a->mod)
        wp_alpha_modifier_v1_destroy(a->mod);
    free(a);
}
#else
void *ssaver_alpha_attach(void *d, void *s) {
    (void)d;
    (void)s;
    return NULL;
}
void ssaver_alpha_set(void *h, float f) {
    (void)h;
    (void)f;
}
void ssaver_alpha_destroy(void *h) { (void)h; }
#endif
