#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Bind zwlr_layer_shell_v1 Overlay on an existing wl_surface.
 * display/surface/output are wayland-client pointers. output may be NULL
 * (compositor picks). Returns 1 on success. */
int ssaver_apply_layer_shell(void *wl_display, void *wl_surface, void *wl_output);

#ifdef __cplusplus
}
#endif
