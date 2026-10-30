#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <xcb/xcb.h>

#ifdef __FreeBSD__
#include <sys/cpuset.h>
#include <sys/rprio.h>
#include <sys/procctl.h>
#include <sys/rctl.h>
#endif

/* =========================================================================
 * Configuration
 * ========================================================================= */
#define MAX_CLIENTS 64
#define WM_NAME "winman"

/* =========================================================================
 * Atoms
 * ========================================================================= */
typedef enum {
    A_NET_SUPPORTED,
    A_NET_CLIENT_LIST,
    A_NET_ACTIVE_WINDOW,
    A_NET_WM_PID,
    A_NET_WM_STATE,
    A_NET_WM_STATE_FULLSCREEN,
    A_NET_WM_NAME,
    A_UTF8_STRING,
    A_WM_PROTOCOLS,
    A_WM_DELETE_WINDOW,
    A_WM_STATE,
    A_LAST
} atom_idx_t;

static const char *atom_names[A_LAST] = {
    "_NET_SUPPORTED",
    "_NET_CLIENT_LIST",
    "_NET_ACTIVE_WINDOW",
    "_NET_WM_PID",
    "_NET_WM_STATE",
    "_NET_WM_STATE_FULLSCREEN",
    "_NET_WM_NAME",
    "UTF8_STRING",
    "WM_PROTOCOLS",
    "WM_DELETE_WINDOW",
    "WM_STATE",
};

static xcb_atom_t atoms[A_LAST];
#define A(i) (atoms[i])

/* =========================================================================
 * Globals
 * ========================================================================= */
static xcb_connection_t *conn;
static xcb_screen_t *screen;
static xcb_window_t root;
static int screen_num;
static volatile sig_atomic_t running = 1;

typedef struct {
    xcb_window_t win;
    pid_t pid;
    int has_pid;
    int allowlisted;
    int stopped;
    int fullscreen;
} client_t;

static client_t clients[MAX_CLIENTS];
static int nclients = 0;
static client_t *foreground = NULL;

/* forward declarations */
static void intern_atoms(void);
static void setup_ewmh(void);
static client_t *find_client(xcb_window_t win);
static client_t *client_add(xcb_window_t win);
static void client_remove(client_t *c);
static void manage(xcb_window_t win);
static void unmanage(client_t *c);
static void configure_window(xcb_window_t win, int16_t x, int16_t y,
                             uint16_t w, uint16_t h);
static void set_fullscreen(client_t *c, int on);
static void focus_client(client_t *c);
static void get_client_pid(client_t *c);
static void freezer_stop(client_t *c);
static void freezer_cont(client_t *c);
static void boost_process(pid_t pid, int foreground_prio);
static void boost_winman(void);
static void boost_foreground(client_t *c);
static void cleanup(void);
static void handle_signal(int sig);
static void run_loop(void);

/* =========================================================================
 * XCB helpers
 * ========================================================================= */

static xcb_atom_t intern_atom(const char *name) {
    xcb_intern_atom_cookie_t c = xcb_intern_atom(conn, 0, strlen(name), name);
    xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(conn, c, NULL);
    if (!r) return XCB_NONE;
    xcb_atom_t a = r->atom;
    free(r);
    return a;
}

static void intern_atoms(void) {
    for (int i = 0; i < A_LAST; i++)
        atoms[i] = intern_atom(atom_names[i]);
}

static void change_prop(xcb_window_t win, xcb_atom_t prop, xcb_atom_t type,
                        int format, uint32_t n, const void *data, uint8_t mode) {
    xcb_change_property(conn, mode, win, prop, type, format, n, data);
}

/* Returns malloc'd array (or NULL). *len set to item count. */
static uint32_t *get_prop_card32(xcb_window_t win, xcb_atom_t prop, uint32_t *len) {
    xcb_get_property_cookie_t c = xcb_get_property(conn, 0, win, prop,
                                                   XCB_GET_PROPERTY_TYPE_ANY,
                                                   0, 1024);
    xcb_get_property_reply_t *r = xcb_get_property_reply(conn, c, NULL);
    if (!r) { *len = 0; return NULL; }
    uint32_t count = xcb_get_property_value_length(r) / 4;
    uint32_t *out = NULL;
    if (count > 0) {
        out = malloc(count * 4);
        memcpy(out, xcb_get_property_value(r), count * 4);
    }
    *len = count;
    free(r);
    return out;
}

static void flush(void) {
    xcb_flush(conn);
}

/* =========================================================================
 * Client list
 * ========================================================================= */

static client_t *find_client(xcb_window_t win) {
    for (int i = 0; i < nclients; i++)
        if (clients[i].win == win) return &clients[i];
    return NULL;
}

static client_t *client_add(xcb_window_t win) {
    if (nclients >= MAX_CLIENTS) return NULL;
    client_t *c = &clients[nclients++];
    memset(c, 0, sizeof(*c));
    c->win = win;
    c->pid = -1;
    c->has_pid = 0;
    c->allowlisted = 0;
    c->stopped = 0;
    c->fullscreen = 0;
    return c;
}

static void client_remove(client_t *c) {
    if (!c) return;
    int idx = -1;
    for (int i = 0; i < nclients; i++) {
        if (&clients[i] == c) { idx = i; break; }
    }
    if (idx < 0) return;
    if (foreground == c) foreground = NULL;
    memmove(&clients[idx], &clients[idx + 1], (nclients - idx - 1) * sizeof(client_t));
    nclients--;
}

/* =========================================================================
 * EWMH / root properties
 * ========================================================================= */

static void setup_ewmh(void) {
    /* Advertise supported atoms on the root window. */
    xcb_atom_t supported[] = {
        A(A_NET_SUPPORTED), A(A_NET_CLIENT_LIST), A(A_NET_ACTIVE_WINDOW),
        A(A_NET_WM_PID), A(A_NET_WM_STATE), A(A_NET_WM_STATE_FULLSCREEN),
        A(A_NET_WM_NAME),
    };
    change_prop(root, A(A_NET_SUPPORTED), XCB_ATOM_ATOM, 32,
                sizeof(supported) / sizeof(xcb_atom_t), supported, XCB_PROP_MODE_REPLACE);

    /* Set WM_NAME for the root (informational). */
    change_prop(root, A(A_NET_WM_NAME), A(A_UTF8_STRING), 8,
                strlen(WM_NAME), WM_NAME, XCB_PROP_MODE_REPLACE);
    flush();
}

static void update_client_list(void) {
    xcb_window_t list[MAX_CLIENTS];
    int n = 0;
    for (int i = 0; i < nclients; i++)
        list[n++] = clients[i].win;
    change_prop(root, A(A_NET_CLIENT_LIST), XCB_ATOM_WINDOW, 32, n, list,
                XCB_PROP_MODE_REPLACE);
    flush();
}

/* =========================================================================
 * Window management
 * ========================================================================= */

static void manage(xcb_window_t win) {
    client_t *c = client_add(win);
    if (!c) {
        fprintf(stderr, "winman: too many clients, unmapping %u\n", win);
        xcb_unmap_window(conn, win);
        flush();
        return;
    }

    /* No title bars: zero border, app fills screen. */
    xcb_change_window_attributes(conn, win, XCB_CW_BORDER_PIXEL | XCB_CW_BACK_PIXEL,
                                 (const uint32_t[]){ screen->black_pixel, screen->black_pixel });
    xcb_configure_window(conn, win, XCB_CONFIG_WINDOW_BORDER_WIDTH,
                         (const uint32_t[]){ 0 });

    /* Resize to full screen. */
    configure_window(win, 0, 0, screen->width_in_pixels, screen->height_in_pixels);

    /* Map and raise. */
    xcb_map_window(conn, win);
    xcb_configure_window(conn, win, XCB_CONFIG_WINDOW_STACK_MODE,
                         (const uint32_t[]){ XCB_STACK_MODE_ABOVE });

    /* Take input focus. */
    focus_client(c);

    /* EWMH: fullscreen state. */
    set_fullscreen(c, 1);

    /* Grab WM_DELETE_WINDOW protocol so we can close gracefully. */
    uint32_t protocols[] = { A(A_WM_DELETE_WINDOW) };
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, A(A_WM_PROTOCOLS),
                        XCB_ATOM_ATOM, 32, 1, protocols);

    /* Allow SubstructureRedirect on the client (reparent to root already). */
    update_client_list();

    /* Read PID (may be set already by client). */
    get_client_pid(c);

    boost_foreground(c);
    flush();

    fprintf(stderr, "winman: managed window %u (foreground)\n", win);
}

static void unmanage(client_t *c) {
    if (!c) return;
    freezer_cont(c); /* never leave stopped */
    client_remove(c);
    update_client_list();
    flush();

    /* Focus next available client, else none. */
    if (nclients > 0) {
        focus_client(&clients[0]);
        boost_foreground(&clients[0]);
    } else {
        foreground = NULL;
        /* Clear active window hint. */
        xcb_window_t none = XCB_WINDOW_NONE;
        change_prop(root, A(A_NET_ACTIVE_WINDOW), XCB_ATOM_WINDOW, 32, 1, &none,
                    XCB_PROP_MODE_REPLACE);
        flush();
    }
}

static void configure_window(xcb_window_t win, int16_t x, int16_t y,
                             uint16_t w, uint16_t h) {
    uint32_t values[] = { (uint32_t)x, (uint32_t)y, w, h, 0 };
    xcb_configure_window(conn, win,
                         XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                         XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT |
                         XCB_CONFIG_WINDOW_BORDER_WIDTH,
                         values);
}

static void set_fullscreen(client_t *c, int on) {
    if (!c) return;
    if (on == c->fullscreen) return;
    c->fullscreen = on;
    /* Set _NET_WM_STATE property on the client window. */
    xcb_atom_t states[1] = { A(A_NET_WM_STATE_FULLSCREEN) };
    change_prop(c->win, A(A_NET_WM_STATE), XCB_ATOM_ATOM, 32,
                on ? 1 : 0, on ? states : NULL, XCB_PROP_MODE_REPLACE);
    /* Configure size. */
    if (on) {
        configure_window(c->win, 0, 0, screen->width_in_pixels,
                         screen->height_in_pixels);
    }
    flush();
}

static void focus_client(client_t *c) {
    if (foreground == c) return;
    if (foreground) {
        /* background the old one */
        freezer_stop(foreground);
    }
    foreground = c;
    if (c) {
        xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, c->win,
                            XCB_CURRENT_TIME);
        xcb_configure_window(conn, c->win, XCB_CONFIG_WINDOW_STACK_MODE,
                             (const uint32_t[]){ XCB_STACK_MODE_ABOVE });
        /* EWMH _NET_ACTIVE_WINDOW */
        change_prop(root, A(A_NET_ACTIVE_WINDOW), XCB_ATOM_WINDOW, 32, 1,
                    (const uint32_t[]){ c->win }, XCB_PROP_MODE_REPLACE);
        freezer_cont(c);
        boost_foreground(c);
        flush();
        fprintf(stderr, "winman: focus -> %u (pid %d)\n", c->win,
                c->has_pid ? (int)c->pid : -1);
    } else {
        xcb_window_t none = XCB_WINDOW_NONE;
        change_prop(root, A(A_NET_ACTIVE_WINDOW), XCB_ATOM_WINDOW, 32, 1, &none,
                    XCB_PROP_MODE_REPLACE);
        flush();
    }
}

/* =========================================================================
 * PID resolution (_NET_WM_PID + XRes fallback stub)
 * ========================================================================= */

static void get_client_pid(client_t *c) {
    if (!c) return;
    c->has_pid = 0;
    c->pid = -1;
    uint32_t *val = NULL;
    uint32_t len = 0;
    val = get_prop_card32(c->win, A(A_NET_WM_PID), &len);
    if (val && len >= 1) {
        c->pid = (pid_t)val[0];
        c->has_pid = 1;
    }
    free(val);
    if (c->has_pid) return;

    /* XRes fallback: best-effort. Without xcb-res dev headers available on
     * the test host this is a stub; on FreeBSD link xcb-res and use
     * xcb_res_query_client_ids to map XID -> client resources -> pid. */
#ifdef __FreeBSD__
    /* TODO: xcb_res fallback to resolve pid from XID when _NET_WM_PID absent. */
#endif
}

/* =========================================================================
 * Freezer (SIGSTOP / SIGCONT on process group)
 * ========================================================================= */

static int kill_pg(pid_t pid, int sig) {
    if (pid <= 0) return -1;
    pid_t pgid = getpgid(pid);
    if (pgid < 0) {
        /* Fall back to the process itself. */
        return kill(pid, sig);
    }
    return kill(-pgid, sig);
}

static void freezer_stop(client_t *c) {
    if (!c || c->stopped) return;
    if (!c->has_pid) return;
    if (c->allowlisted) return;
    if (kill_pg(c->pid, SIGSTOP) == 0) {
        c->stopped = 1;
        fprintf(stderr, "winman: SIGSTOP pg of pid %d (win %u)\n",
                (int)c->pid, c->win);
    } else {
        fprintf(stderr, "winman: SIGSTOP failed pid %d: %s\n",
                (int)c->pid, strerror(errno));
    }
}

static void freezer_cont(client_t *c) {
    if (!c || !c->stopped) return;
    if (!c->has_pid) return;
    if (kill_pg(c->pid, SIGCONT) == 0) {
        c->stopped = 0;
        fprintf(stderr, "winman: SIGCONT pg of pid %d (win %u)\n",
                (int)c->pid, c->win);
    } else {
        fprintf(stderr, "winman: SIGCONT failed pid %d: %s\n",
                (int)c->pid, strerror(errno));
    }
}

static void freezer_cont_all(void) {
    for (int i = 0; i < nclients; i++)
        freezer_cont(&clients[i]);
}

/* =========================================================================
 * Prioritisation / cpuset (FreeBSD-specific, guarded)
 * ========================================================================= */

static void boost_process(pid_t pid, int foreground_prio) {
#ifdef __FreeBSD__
    /* Nice value: lower = higher priority. Foreground gets a boost. */
    int nice_val = foreground_prio ? -10 : 4;
    setpriority(PRIO_PROCESS, pid, nice_val);

    /* Real-time priority via rtprio(2): RTP_PRIO_REALTIME for foreground. */
    struct rtprio rt;
    memset(&rt, 0, sizeof(rt));
    if (foreground_prio) {
        rt.type = RTP_PRIO_REALTIME;
        rt.prio = 30;
    } else {
        rt.type = RTP_PRIO_NORMAL;
        rt.prio = 0;
    }
    rtprio(RTP_SET, pid, &rt);

    /* CPU affinity: reserve core 0 for winman+foreground on multi-core. */
    cpuset_t mask;
    CPU_ZERO(&mask);
    if (foreground_prio) CPU_SET(0, &mask);
    else {
        long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
        for (long i = 0; i < ncpu; i++) CPU_SET(i, &mask);
    }
    cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, -1, sizeof(mask), &mask);

    /* procctl: keep alive / resource controls where useful. */
    (void)0;
#else
    /* Linux test host: use setpriority + sched affinity best-effort. */
    int nice_val = foreground_prio ? -10 : 4;
    setpriority(PRIO_PROCESS, pid, nice_val);
#endif
}

static void boost_winman(void) {
    boost_process(getpid(), 1);
}

static void boost_foreground(client_t *c) {
    if (!c || !c->has_pid) return;
    /* Also boost the whole process group (tree) for multi-process apps. */
    pid_t pgid = getpgid(c->pid);
    if (pgid > 0) boost_process(pgid, 1);
    boost_process(c->pid, 1);
}

/* =========================================================================
 * Lifecycle / signals
 * ========================================================================= */

static void cleanup(void) {
    freezer_cont_all();
    /* Restore root properties / remove grabs implicitly by disconnecting. */
    flush();
    if (conn) {
        xcb_disconnect(conn);
        conn = NULL;
    }
    fprintf(stderr, "winman: cleaned up\n");
}

static void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

/* =========================================================================
 * Event loop
 * ========================================================================= */

static void handle_map_request(xcb_map_request_event_t *ev) {
    client_t *existing = find_client(ev->window);
    if (existing) {
        /* Already managed; just map + focus. */
        xcb_map_window(conn, ev->window);
        xcb_configure_window(conn, ev->window, XCB_CONFIG_WINDOW_STACK_MODE,
                             (const uint32_t[]){ XCB_STACK_MODE_ABOVE });
        focus_client(existing);
        boost_foreground(existing);
        flush();
        return;
    }
    manage(ev->window);
}

static void handle_unmap_notify(xcb_unmap_notify_event_t *ev) {
    client_t *c = find_client(ev->window);
    if (c) unmanage(c);
}

static void handle_destroy_notify(xcb_destroy_notify_event_t *ev) {
    client_t *c = find_client(ev->window);
    if (c) unmanage(c);
}

static void handle_client_message(xcb_client_message_event_t *ev) {
    if (ev->window == XCB_WINDOW_NONE) return;
    client_t *c = find_client(ev->window);
    if (!c) return;

    xcb_atom_t type = ev->type;
    long d0 = (long)ev->data.data32[0];

    if (type == A(A_WM_PROTOCOLS)) {
        if ((xcb_atom_t)d0 == A(A_WM_DELETE_WINDOW)) {
            /* Close requested: SIGCONT first, then terminate gracefully. */
            freezer_cont(c);
            /* Send WM_DELETE_WINDOW back to the client so it can exit itself. */
            xcb_client_message_event_t cm;
            memset(&cm, 0, sizeof(cm));
            cm.response_type = XCB_CLIENT_MESSAGE;
            cm.format = 32;
            cm.window = c->win;
            cm.type = A(A_WM_PROTOCOLS);
            cm.data.data32[0] = A(A_WM_DELETE_WINDOW);
            cm.data.data32[1] = XCB_CURRENT_TIME;
            xcb_send_event(conn, 0, c->win, XCB_EVENT_MASK_NO_EVENT, (const char *)&cm);
            flush();
            unmanage(c);
        }
        return;
    }

    if (type == A(A_NET_ACTIVE_WINDOW)) {
        /* Client requests to be activated. */
        focus_client(c);
        boost_foreground(c);
        return;
    }

    if (type == A(A_NET_WM_STATE)) {
        long action = (long)ev->data.data32[0];
        xcb_atom_t state1 = (xcb_atom_t)ev->data.data32[1];
        xcb_atom_t state2 = (xcb_atom_t)ev->data.data32[2];
        int on = (action == 1); /* _NET_WM_STATE_ADD */
        if (state1 == A(A_NET_WM_STATE_FULLSCREEN) || state2 == A(A_NET_WM_STATE_FULLSCREEN)) {
            set_fullscreen(c, on);
        }
        return;
    }
}

static void handle_property_notify(xcb_property_notify_event_t *ev) {
    if (ev->window == root) return;
    client_t *c = find_client(ev->window);
    if (!c) return;
    if (ev->atom == A(A_NET_WM_PID)) {
        get_client_pid(c);
        if (c == foreground) boost_foreground(c);
    }
}

static void handle_focus_in(xcb_focus_in_event_t *ev) {
    /* We manage focus explicitly via focus_client(); X server focus events
     * are acknowledged but we don't drive policy from them to avoid fights
     * with clients setting their own focus. */
    (void)ev;
}

static void run_loop(void) {
    xcb_generic_event_t *ev;
    while (running && (ev = xcb_wait_for_event(conn))) {
        uint8_t type = ev->response_type & ~0x80;
        switch (type) {
        case XCB_MAP_REQUEST:
            handle_map_request((xcb_map_request_event_t *)ev);
            break;
        case XCB_UNMAP_NOTIFY:
            handle_unmap_notify((xcb_unmap_notify_event_t *)ev);
            break;
        case XCB_DESTROY_NOTIFY:
            handle_destroy_notify((xcb_destroy_notify_event_t *)ev);
            break;
        case XCB_CLIENT_MESSAGE:
            handle_client_message((xcb_client_message_event_t *)ev);
            break;
        case XCB_PROPERTY_NOTIFY:
            handle_property_notify((xcb_property_notify_event_t *)ev);
            break;
        case XCB_FOCUS_IN:
            handle_focus_in((xcb_focus_in_event_t *)ev);
            break;
        default:
            break;
        }
        free(ev);
        flush();
    }
}

/* =========================================================================
 * Main
 * ========================================================================= */

int main(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGCHLD, &(struct sigaction){ .sa_handler = SIG_IGN }, NULL);

    conn = xcb_connect(NULL, &screen_num);
    if (!conn || xcb_connection_has_error(conn)) {
        fprintf(stderr, "winman: cannot open display\n");
        return 1;
    }

    const xcb_setup_t *setup = xcb_get_setup(conn);
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
    for (int i = 0; i < screen_num; i++) xcb_screen_next(&it);
    screen = it.data;
    root = screen->root;

    intern_atoms();

    /* Become the WM: redirect substructure so we get MapRequest etc. */
    uint32_t mask = XCB_CW_EVENT_MASK;
    uint32_t values[] = {
        XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
        XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
        XCB_EVENT_MASK_FOCUS_CHANGE |
        XCB_EVENT_MASK_PROPERTY_CHANGE |
        XCB_EVENT_MASK_STRUCTURE_NOTIFY
    };
    xcb_change_window_attributes(conn, root, mask, values);

    /* We also want to know when the root gets focus changes. */
    xcb_grab_server(conn);
    setup_ewmh();
    xcb_ungrab_server(conn);
    flush();

    boost_winman();

    fprintf(stderr, "winman: running on screen %d root=%u\n", screen_num, root);

    run_loop();

    cleanup();
    return 0;
}
