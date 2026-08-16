#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Bind wp_alpha_modifier_v1 on a wl_surface. Returns NULL if missing. */
void *ssaver_alpha_attach(void *wl_display, void *wl_surface);

/* factor 0 = transparent, 1 = opaque. Compositor blends on next commit. */
void ssaver_alpha_set(void *handle, float factor);

void ssaver_alpha_destroy(void *handle);

#ifdef __cplusplus
}
#endif
