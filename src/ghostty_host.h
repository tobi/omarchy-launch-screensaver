#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t ch;
    uint8_t fg_r, fg_g, fg_b;
    uint8_t bg_r, bg_g, bg_b;
} GhosttyHostCell;

void *ghostty_host_create(int cols, int rows);
void ghostty_host_destroy(void *h);
/* Feed a full VT frame; fill cells (row-major). Returns 1 on success. */
int ghostty_host_feed(void *h, const uint8_t *data, size_t len,
                      GhosttyHostCell *cells, int cols, int rows);
#ifdef __cplusplus
}
#endif
