#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xcb/xcb.h>

static xcb_connection_t *conn;
static xcb_window_t win;
static xcb_screen_t *screen;
static xcb_gcontext_t gc;
static xcb_font_t font;
static volatile int running = 1;

enum {
    A_NET_WM_PID,
    A_NET_WM_STATE,
    A_NET_WM_STATE_FULLSCREEN,
    A_WM_PROTOCOLS,
    A_WM_DELETE_WINDOW,
    A_LAST
};
static const char *names[A_LAST] = {
    "_NET_WM_PID", "_NET_WM_STATE", "_NET_WM_STATE_FULLSCREEN",
    "WM_PROTOCOLS", "WM_DELETE_WINDOW"
};
static xcb_atom_t atoms[A_LAST];

static xcb_atom_t intern(const char *n) {
    xcb_intern_atom_cookie_t c = xcb_intern_atom(conn, 0, strlen(n), n);
    xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(conn, c, NULL);
    xcb_atom_t a = r ? r->atom : XCB_NONE;
    free(r);
    return a;
}

static void redraw(void) {
    int w = screen->width_in_pixels;
    int h = screen->height_in_pixels;

    /* Clear background. */
    xcb_poly_fill_rectangle(conn, win, gc, 1,
                            (const xcb_rectangle_t[]){ {0, 0, (uint16_t)w, (uint16_t)h} });

    /* Status bar (like Android 4.4). */
    xcb_change_gc(conn, gc, XCB_GC_FOREGROUND,
                  (const uint32_t[]){ 0xff33b5e5 }); /* android blue */
    xcb_poly_fill_rectangle(conn, win, gc, 1,
                            (const xcb_rectangle_t[]){ {0, 0, (uint16_t)w, 40} });
    xcb_change_gc(conn, gc, XCB_GC_FOREGROUND,
                  (const uint32_t[]){ screen->white_pixel });
    xcb_image_text_8(conn, strlen("winos home"), win, gc, 20, 26, "winos home");

    /* Grid of app icons. */
    int cols = 4, rows = 4;
    int icon_w = w / cols;
    int icon_h = (h - 80) / rows;
    const uint32_t colors[] = { 0xff4caf50, 0xff2196f3, 0xff9c27b0,
                                0xffff9800, 0xfff44336, 0xff009688,
                                0xff795548, 0xff607d8b, 0xffffeb3b,
                                0xff9e9e9e, 0xff8bc34a, 0xff03a9f4 };
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int idx = (r * cols + c) % (sizeof(colors) / sizeof(colors[0]));
            xcb_rectangle_t rect = {
                (uint16_t)(c * icon_w + 10),
                (uint16_t)(80 + r * icon_h + 10),
                (uint16_t)(icon_w - 20),
                (uint16_t)(icon_h - 20)
            };
            xcb_change_gc(conn, gc, XCB_GC_FOREGROUND, (const uint32_t[]){ colors[idx] });
            xcb_poly_fill_rectangle(conn, win, gc, 1, &rect);
            char label[16];
            snprintf(label, sizeof(label), "App %d", r * cols + c + 1);
            xcb_change_gc(conn, gc, XCB_GC_FOREGROUND,
                          (const uint32_t[]){ screen->white_pixel });
            xcb_image_text_8(conn, strlen(label), win, gc,
                             rect.x + 4, rect.y + rect.height / 2 + 4, label);
        }
    }
}

int main(void) {
    conn = xcb_connect(NULL, NULL);
    if (!conn || xcb_connection_has_error(conn)) return 1;

    xcb_screen_iterator_t it = xcb_setup_roots_iterator(xcb_get_setup(conn));
    screen = it.data;

    for (int i = 0; i < A_LAST; i++) atoms[i] = intern(names[i]);

    win = xcb_generate_id(conn);
    uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    uint32_t values[] = { screen->black_pixel,
                          XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS |
                          XCB_EVENT_MASK_STRUCTURE_NOTIFY };
    xcb_create_window(conn, XCB_COPY_FROM_PARENT, win, screen->root,
                      0, 0, screen->width_in_pixels, screen->height_in_pixels,
                      0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
                      mask, values);

    /* Advertise PID so winman can freeze/thaw us. */
    uint32_t pid = (uint32_t)getpid();
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, atoms[A_NET_WM_PID],
                        XCB_ATOM_CARDINAL, 32, 1, &pid);

    /* Request fullscreen state. */
    xcb_atom_t fs[] = { atoms[A_NET_WM_STATE_FULLSCREEN] };
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, atoms[A_NET_WM_STATE],
                        XCB_ATOM_ATOM, 32, 1, fs);

    /* WM_DELETE_WINDOW protocol. */
    uint32_t protos[] = { atoms[A_WM_DELETE_WINDOW] };
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, atoms[A_WM_PROTOCOLS],
                        XCB_ATOM_ATOM, 32, 1, protos);

    xcb_map_window(conn, win);
    xcb_flush(conn);

    gc = xcb_generate_id(conn);
    xcb_create_gc(conn, gc, win, 0, NULL);
    xcb_change_gc(conn, gc, XCB_GC_FOREGROUND,
                  (const uint32_t[]){ screen->white_pixel });

    font = xcb_generate_id(conn);
    xcb_open_font(conn, font, strlen("fixed"), "fixed");

    while (running) {
        xcb_generic_event_t *ev = xcb_wait_for_event(conn);
        if (!ev) break;
        uint8_t type = ev->response_type & ~0x80;
        if (type == XCB_EXPOSE) {
            redraw();
        } else if (type == XCB_KEY_PRESS) {
            running = 0;
        } else if (type == XCB_CLIENT_MESSAGE) {
            xcb_client_message_event_t *cm = (xcb_client_message_event_t *)ev;
            if ((xcb_atom_t)cm->data.data32[0] == atoms[A_WM_DELETE_WINDOW])
                running = 0;
        }
        free(ev);
        xcb_flush(conn);
    }

    xcb_close_font(conn, font);
    xcb_free_gc(conn, gc);
    xcb_destroy_window(conn, win);
    xcb_disconnect(conn);
    return 0;
}
