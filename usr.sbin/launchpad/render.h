#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>

struct render {
    uint8_t *pix;   /* RGB24, row-major, [h][w][3] */
    int w, h;
    int stride;
};

int  render_init(struct render *r, int w, int h);
void render_fini(struct render *r);
void render_clear(struct render *r, uint32_t color); /* 0x00RRGGBB */
void render_rect(struct render *r, int x, int y, int w, int h, uint32_t color);
int  render_write_ppm(struct render *r, const char *path);

#endif /* RENDER_H */
