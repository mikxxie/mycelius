#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

static uint8_t c_r(uint32_t c) { return (uint8_t)((c >> 16) & 0xff); }
static uint8_t c_g(uint32_t c) { return (uint8_t)((c >> 8)  & 0xff); }
static uint8_t c_b(uint32_t c) { return (uint8_t)(c & 0xff); }

int render_init(struct render *r, int w, int h) {
    memset(r, 0, sizeof(*r));
    r->w = w;
    r->h = h;
    r->stride = w * 3;
    r->pix = calloc((size_t)h, (size_t)r->stride);
    if (!r->pix) return -1;
    return 0;
}

void render_fini(struct render *r) {
    free(r->pix);
    r->pix = NULL;
    r->w = r->h = r->stride = 0;
}

void render_clear(struct render *r, uint32_t color) {
    uint8_t rr = c_r(color), gg = c_g(color), bb = c_b(color);
    for (int y = 0; y < r->h; y++) {
        uint8_t *row = r->pix + (size_t)y * r->stride;
        for (int x = 0; x < r->w; x++) {
            row[x * 3 + 0] = rr;
            row[x * 3 + 1] = gg;
            row[x * 3 + 2] = bb;
        }
    }
}

void render_rect(struct render *r, int x, int y, int w, int h, uint32_t color) {
    uint8_t rr = c_r(color), gg = c_g(color), bb = c_b(color);
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > r->w ? r->w : x + w;
    int y1 = y + h > r->h ? r->h : y + h;
    for (int yy = y0; yy < y1; yy++) {
        uint8_t *row = r->pix + (size_t)yy * r->stride + (size_t)x0 * 3;
        for (int xx = x0; xx < x1; xx++) {
            row[(xx - x0) * 3 + 0] = rr;
            row[(xx - x0) * 3 + 1] = gg;
            row[(xx - x0) * 3 + 2] = bb;
        }
    }
}

int render_write_ppm(struct render *r, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", r->w, r->h);
    fwrite(r->pix, 1, (size_t)r->h * (size_t)r->stride, f);
    fclose(f);
    return 0;
}
