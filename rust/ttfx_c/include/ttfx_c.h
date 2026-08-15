#ifndef TTFX_C_H
#define TTFX_C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TtfxEngine TtfxEngine;

typedef struct TtfxConfig {
    const char *input;
    int32_t cols;
    int32_t rows;
    const char *effect; /* NULL / "random" = random */
    int32_t frame_rate; /* 0 = 120 */
    int32_t canvas_width;  /* 0 = cols */
    int32_t canvas_height; /* 0 = rows */
    int32_t reuse_canvas;
    const char *anchor_canvas; /* "c" default */
    const char *anchor_text;
    int32_t no_eol;
    int32_t no_restore_cursor;
    int32_t has_seed;
    uint64_t seed;
    const char *include_effects; /* comma/space list, nullable */
    const char *exclude_effects;
} TtfxConfig;

/* Create an in-process ttfx engine.
 * cols/rows set the canvas (ignore tty size — Omarchy's 80x24 bug).
 * effect: effect name, or NULL / "random" for a random effect.
 * Returns NULL on error; see ttfx_last_error(). */
TtfxEngine *ttfx_create(const char *input, int cols, int rows, const char *effect);

/* Same, with the rest of the ttfx terminal options Omarchy/users pass. */
TtfxEngine *ttfx_create_ex(const TtfxConfig *cfg);

void ttfx_destroy(TtfxEngine *eng);

/* 1 = frame ready (*data, *len valid until next call / destroy)
 * 0 = effect finished
 * -1 = error */
int ttfx_next_frame(TtfxEngine *eng, const uint8_t **data, size_t *len);

const char *ttfx_effect_name(const TtfxEngine *eng);
const char *ttfx_last_error(void);

/* Comma-separated effect names. Valid until the next call. */
const char *ttfx_effect_names(void);

#ifdef __cplusplus
}
#endif

#endif
