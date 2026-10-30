#ifndef LP_PROTO_H
#define LP_PROTO_H

#include <stdint.h>

/*
 * LaunchPad / backboardd IPC protocol.
 *
 * backboardd is the low-level input daemon (evdev digitiser, buttons,
 * sensors). It serialises raw hardware events and streams them to
 * LaunchPad over a Unix domain socket. LaunchPad is the UI shell /
 * render server: it owns the scene graph, hit-tests touches, composites
 * the frame buffer, and forwards touch to the foreground application.
 *
 * v0 keeps the wire format POD so it crosses the FreeBSD <-> Linux CI
 * boundary and aarch64 unknown-freebsd target unchanged.
 */

#define LP_BACKBOARD_SOCK "/var/run/lp_bb.sock"
#define LP_APP_SOCK       "/var/run/lp_app.sock"

#define LP_FRAME_W 480
#define LP_FRAME_H 800   /* iPhone-like portrait */
#define LP_VSYNC_MS 16   /* ~60 Hz render cadence */

enum bb_event_type {
    BB_EV_TOUCH_DOWN = 1,
    BB_EV_TOUCH_UP   = 2,
    BB_EV_TOUCH_MOVE = 3,
    BB_EV_KEY        = 4,
};

/*
 * backboardd -> LaunchPad. One per input event.
 */
struct bb_event {
    uint32_t type;        /* enum bb_event_type */
    int32_t  x;
    int32_t  y;
    uint64_t timestamp_ns;
    uint32_t pad;
} __attribute__((packed));

enum lp_msg_type {
    LP_MSG_LAYER_UPDATE = 1,  /* app -> LaunchPad: my layer rect/color */
    LP_MSG_TOUCH_FORWARD = 2, /* LaunchPad -> app: touch for you */
    LP_MSG_FRAME_DONE    = 3, /* LaunchPad -> app: vsync, draw next */
};

/*
 * app <-> LaunchPad layer/update message.
 */
struct lp_msg {
    uint32_t type;        /* enum lp_msg_type */
    uint32_t app_id;      /* foreground app id */
    int32_t  x, y, w, h;  /* layer rect (screen space) */
    uint32_t color;       /* 0x00RRGGBB fill for v0 */
    uint64_t timestamp_ns;
} __attribute__((packed));

#endif /* LP_PROTO_H */
