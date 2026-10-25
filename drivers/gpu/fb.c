#ifndef DRIVERS_GPU_FB_H
#define DRIVERS_GPU_FB_H

#include "kernel.h"

/* Flat framebuffer driver - writes directly to RAM framebuffer.
 * QEMU's virtio-gpu can scan out this memory via RESOURCE_ATTACH_BACKING
 * and RESOURCE_SET_SCANOUT.
 *
 * Framebuffer format: 320x240x32, XRGB8888 (little-endian).
 * Memory layout: row-major, 4 bytes per pixel.
 */

#define FB_WIDTH  320
#define FB_HEIGHT 240
#define FB_BPP    4
#define FB_SIZE   (FB_WIDTH * FB_HEIGHT * FB_BPP)

/* Framebuffer physical address - must be identity-mapped (VA == PA) */
extern u32 fb_buf[];
extern u32  fb_pa;

/* Colors in AABBGGRR format (little-endian u32 stored as R,G,B,A in memory) */
#define FB_BLACK  0xFF000000
#define FB_WHITE  0xFFFFFFFF
#define FB_BLUE   0xFFFF0000
#define FB_GREEN  0xFF00FF00
#define FB_RED    0xFF0000FF
#define FB_YELLOW 0xFF00FFFF

void fb_init(void);
void fb_pixel(u32 x, u32 y, u32 color);
void fb_rect(u32 x, u32 y, u32 w, u32 h, u32 color);
void fb_text(u32 x, u32 y, const char *s, u32 fg, u32 bg);
void fb_flush(void);

u32 fb_get_addr(void);
u32 fb_get_width(void);
u32 fb_get_height(void);

#endif /* DRIVERS_GPU_FB_H */