#include "ghostty_host.h"

#ifndef HAVE_LIBGHOSTTY
void *ghostty_host_create(int cols, int rows) {
    (void)cols;
    (void)rows;
    return 0;
}
void ghostty_host_destroy(void *h) { (void)h; }
int ghostty_host_feed(void *h, const uint8_t *data, size_t len,
                      GhosttyHostCell *cells, int cols, int rows) {
    (void)h;
    (void)data;
    (void)len;
    (void)cells;
    (void)cols;
    (void)rows;
    return 0;
}
#else
#include <ghostty/vt.h>
#include <stdlib.h>
#include <string.h>

struct host {
    GhosttyTerminal term;
    GhosttyRenderState rs;
    GhosttyRenderStateRowIterator row_it;
    GhosttyRenderStateRowCells row_cells;
};

void *ghostty_host_create(int cols, int rows) {
    struct host *h = calloc(1, sizeof(*h));
    if (!h)
        return 0;
    if (ghostty_terminal_new(NULL, &h->term, (uint16_t)cols, (uint16_t)rows) != GHOSTTY_SUCCESS) {
        free(h);
        return 0;
    }
    if (ghostty_render_state_new(NULL, &h->rs) != GHOSTTY_SUCCESS) {
        ghostty_terminal_free(h->term);
        free(h);
        return 0;
    }
    GhosttyColorRgb bg = {0, 0, 0};
    GhosttyColorRgb fg = {220, 220, 220};
    ghostty_terminal_set(h->term, GHOSTTY_TERMINAL_OPT_COLOR_BACKGROUND, &bg);
    ghostty_terminal_set(h->term, GHOSTTY_TERMINAL_OPT_COLOR_FOREGROUND, &fg);
    return h;
}

void ghostty_host_destroy(void *p) {
    struct host *h = p;
    if (!h)
        return;
    if (h->row_cells)
        ghostty_render_state_row_cells_free(h->row_cells);
    if (h->row_it)
        ghostty_render_state_row_iterator_free(h->row_it);
    if (h->rs)
        ghostty_render_state_free(h->rs);
    if (h->term)
        ghostty_terminal_free(h->term);
    free(h);
}

int ghostty_host_feed(void *p, const uint8_t *data, size_t len,
                      GhosttyHostCell *cells, int cols, int rows) {
    struct host *h = p;
    if (!h || !cells)
        return 0;
    ghostty_terminal_reset(h->term);
    ghostty_terminal_vt_write(h->term, data, len);
    if (ghostty_render_state_update(h->rs, h->term) != GHOSTTY_SUCCESS)
        return 0;
    if (!h->row_it) {
        if (ghostty_render_state_row_iterator_new(NULL, &h->row_it) != GHOSTTY_SUCCESS)
            return 0;
    }
    if (ghostty_render_state_get(h->rs, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &h->row_it) !=
        GHOSTTY_SUCCESS)
        return 0;
    if (!h->row_cells) {
        if (ghostty_render_state_row_cells_new(NULL, &h->row_cells) != GHOSTTY_SUCCESS)
            return 0;
    }
    GhosttyColorRgb def_fg = {220, 220, 220};
    GhosttyColorRgb def_bg = {0, 0, 0};
    ghostty_render_state_get(h->rs, GHOSTTY_RENDER_STATE_DATA_COLOR_FOREGROUND, &def_fg);
    ghostty_render_state_get(h->rs, GHOSTTY_RENDER_STATE_DATA_COLOR_BACKGROUND, &def_bg);

    memset(cells, 0, (size_t)cols * (size_t)rows * sizeof(*cells));
    int y = 0;
    while (ghostty_render_state_row_iterator_next(h->row_it) && y < rows) {
        if (ghostty_render_state_row_get(h->row_it, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS,
                                         &h->row_cells) != GHOSTTY_SUCCESS) {
            ++y;
            continue;
        }
        int x = 0;
        while (ghostty_render_state_row_cells_next(h->row_cells) && x < cols) {
            GhosttyHostCell *c = &cells[(size_t)y * (size_t)cols + (size_t)x];
            uint32_t cps[8] = {0};
            if (ghostty_render_state_row_cells_get(
                    h->row_cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF, cps) ==
                GHOSTTY_SUCCESS)
                c->ch = cps[0] ? cps[0] : ' ';
            else
                c->ch = ' ';
            GhosttyColorRgb fg = def_fg, bg = def_bg;
            if (ghostty_render_state_row_cells_get(
                    h->row_cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR, &fg) != GHOSTTY_SUCCESS)
                fg = def_fg;
            if (ghostty_render_state_row_cells_get(
                    h->row_cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR, &bg) != GHOSTTY_SUCCESS)
                bg = def_bg;
            c->fg_r = fg.r;
            c->fg_g = fg.g;
            c->fg_b = fg.b;
            c->bg_r = bg.r;
            c->bg_g = bg.g;
            c->bg_b = bg.b;
            ++x;
        }
        ++y;
    }
    return 1;
}
#endif
