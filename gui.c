#include "kernel.h"

/* Super simple software GUI — framebuffer lives in RAM, ready for a future
 * virtio-gpu driver to blit it to screen.  For now the `gui` command draws a
 * demo pattern into the framebuffer and reports its address. */

#define FB_WIDTH  320
#define FB_HEIGHT 240
#define FB_BPP    4   /* 32-bit RGBA */
#define FB_SIZE   (FB_WIDTH * FB_HEIGHT * FB_BPP)

static u32 *fb = 0;
static u32  fb_pa = 0;

#define COLOR_BLACK  0x000000FF
#define COLOR_WHITE  0xFFFFFFFF
#define COLOR_BLUE   0x0000FFFF
#define COLOR_GREEN  0x00FF00FF
#define COLOR_RED    0xFF0000FF
#define COLOR_YELLOW 0xFFFF00FF

static void fb_pixel(u32 x, u32 y, u32 color) {
    if (x >= FB_WIDTH || y >= FB_HEIGHT || !fb) return;
    fb[y * FB_WIDTH + x] = color;
}

static void fb_rect(u32 x, u32 y, u32 w, u32 h, u32 color) {
    for (u32 dy = 0; dy < h; ++dy)
        for (u32 dx = 0; dx < w; ++dx)
            fb_pixel(x + dx, y + dy, color);
}

static void fb_char(u32 x, u32 y, char c, u32 fg, u32 bg) {
    /* 5x7 bitmap font.  Bit set = foreground. */
    static const u8 font[95][7] = {
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* space */
        {0x00,0x00,0x2F,0x00,0x00,0x00,0x00}, /* ! */
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* " */
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* # */
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* $ */
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* % */
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* & */
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* ' */
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* ( */
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* ) */
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* * */
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* + */
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* , */
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* - */
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* . */
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* / */
        {0x3E,0x63,0x73,0x73,0x73,0x67,0x3E}, /* 0 */
        {0x0C,0x1C,0x1C,0x0C,0x0C,0x0C,0x3E}, /* 1 */
        {0x3E,0x63,0x03,0x0E,0x1C,0x30,0x3E}, /* 2 */
        {0x3E,0x63,0x03,0x1E,0x03,0x63,0x3E}, /* 3 */
        {0x0E,0x1E,0x3E,0x63,0x63,0x63,0x3E}, /* 4 */
        {0x3E,0x30,0x3E,0x03,0x03,0x63,0x3E}, /* 5 */
        {0x1C,0x30,0x3E,0x33,0x03,0x63,0x3E}, /* 6 */
        {0x3E,0x03,0x03,0x06,0x0C,0x0C,0x0C}, /* 7 */
        {0x3E,0x63,0x3E,0x33,0x03,0x63,0x3E}, /* 8 */
        {0x3E,0x63,0x3E,0x03,0x03,0x63,0x3E}, /* 9 */
    };
    if (!fb || c < 32 || c > 126) return;
    u8 glyph[7];
    for (int i = 0; i < 7; ++i) glyph[i] = 0;
    for (int i = 0; i < 7; ++i) glyph[i] = font[c - 32][i];
    for (u32 row = 0; row < 7; ++row) {
        u8 bits = glyph[row];
        for (u32 col = 0; col < 5; ++col) {
            if (bits & (1 << (4 - col)))
                fb_pixel(x + col, y + row, fg);
            else
                fb_pixel(x + col, y + row, bg);
        }
    }
}

static void fb_text(u32 x, u32 y, const char *s, u32 fg, u32 bg) {
    u32 cx = x;
    while (*s) {
        fb_char(cx, y, *s, fg, bg);
        cx += 6;
        ++s;
    }
}

void gui_init(void) {
    if (fb) return;
    u8 *page = (u8 *)page_alloc();
    if (!page) return;
    fb = (u32 *)page;
    fb_pa = (u64)page;
    for (u32 i = 0; i < FB_WIDTH * FB_HEIGHT; ++i) fb[i] = COLOR_BLACK;
}

u32 gui_fb_addr(void) { return fb_pa; }
u32 gui_fb_width(void) { return FB_WIDTH; }
u32 gui_fb_height(void) { return FB_HEIGHT; }

void gui_demo(void) {
    if (!fb) gui_init();
    if (!fb) { uart_puts("gui: no framebuffer memory\r\n"); return; }

    /* Background */
    fb_rect(0, 0, FB_WIDTH, FB_HEIGHT, COLOR_BLUE);
    /* Border */
    fb_rect(0, 0, FB_WIDTH, 4, COLOR_WHITE);
    fb_rect(0, 0, 4, FB_HEIGHT, COLOR_WHITE);
    fb_rect(0, FB_HEIGHT - 4, FB_WIDTH, 4, COLOR_WHITE);
    fb_rect(FB_WIDTH - 4, 0, 4, FB_HEIGHT, COLOR_WHITE);
    /* Title */
    fb_text(100, 20, "Mycelius GUI", COLOR_YELLOW, COLOR_BLUE);
    /* Demo rectangles */
    fb_rect(40, 80, 60, 40, COLOR_RED);
    fb_rect(120, 80, 60, 40, COLOR_GREEN);
    fb_rect(200, 80, 60, 40, COLOR_YELLOW);
    /* Stats text */
    fb_text(20, 160, "FB addr: 0x", COLOR_WHITE, COLOR_BLUE);
    /* Print address hex */
    char hex[9];
    u32 v = fb_pa;
    for (int i = 7; i >= 0; --i) {
        u32 d = (v >> (i * 4)) & 0xF;
        hex[7 - i] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
    }
    hex[8] = 0;
    fb_text(100, 160, hex, COLOR_WHITE, COLOR_BLUE);
    fb_text(20, 180, "Size: 320x240x32", COLOR_WHITE, COLOR_BLUE);

    uart_puts("gui: framebuffer demo drawn at 0x");
    for (int i = 28; i >= 0; i -= 4) {
        u32 d = (u32)((fb_pa >> i) & 0xf);
        char c[2] = { d < 10 ? '0' + d : 'a' + d - 10, 0 };
        uart_puts(c);
    }
    uart_puts(" (320x240x32)\r\n");
}
