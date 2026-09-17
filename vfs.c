#include "kernel.h"

/* Small, RAM-only VFS fallback when no ext4 disk is attached. Once ext4 mounts,
 * the same shell commands operate on the block device. */
#define VFS_NODES 24
#define NAME_MAX 15
struct node { u8 used, parent, first_child, next; char name[NAME_MAX + 1]; };
static struct node nodes[VFS_NODES];
static u8 cwd;
static int disk;
#define NONE 0xff

static int eq(const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
static void name_copy(char *to, const char *from) { u32 i = 0; while (from[i] && i < NAME_MAX) { to[i] = from[i]; ++i; } to[i] = 0; }
static u8 child(u8 parent, const char *name) {
    for (u8 n = nodes[parent].first_child; n != NONE; n = nodes[n].next) if (eq(nodes[n].name, name)) return n;
    return NONE;
}
static u8 resolve(const char *path) {
    u8 at = *path == '/' ? 0 : cwd; char piece[NAME_MAX + 1];
    while (*path) {
        u32 n = 0; while (*path == '/') ++path; if (!*path) break;
        while (*path && *path != '/' && n < NAME_MAX) piece[n++] = *path++;
        piece[n] = 0;
        if (eq(piece, ".")) continue;
        if (eq(piece, "..")) { at = nodes[at].parent; continue; }
        at = child(at, piece); if (at == NONE) return NONE;
    }
    return at;
}
static void path(u8 n) {
    if (!n) { uart_puts("/"); return; }
    path(nodes[n].parent); uart_puts(nodes[n].parent ? "/" : ""); uart_puts(nodes[n].name);
}
void vfs_init(void) {
    for (u32 i = 0; i < VFS_NODES; ++i) nodes[i].used = 0;
    nodes[0].used = 1; nodes[0].parent = 0; nodes[0].first_child = NONE; nodes[0].next = NONE; nodes[0].name[0] = 0;
    cwd = 0;
    disk = 0;
}
void vfs_use_ext4(void) { disk = 1; }
void vfs_pwd(void) {
    if (disk && ext4_mounted()) { ext4_pwd(); return; }
    path(cwd); uart_puts("\r\n");
}
void vfs_ls(void) {
    if (disk && ext4_mounted()) { (void)ext4_ls(); return; }
    for (u8 n = nodes[cwd].first_child; n != NONE; n = nodes[n].next) { uart_puts(nodes[n].name); uart_puts("/  "); }
    uart_puts("\r\n");
}
int vfs_ls_path(const char *p) {
    if (disk && ext4_mounted()) return ext4_ls_path(p);
    u8 old = cwd, at = resolve(p); if (at == NONE) return -1;
    cwd = at; vfs_ls(); cwd = old; return 0;
}
int vfs_cd(const char *name) {
    if (disk && ext4_mounted()) return ext4_cd(name);
    u8 n = resolve(name); if (n == NONE) return -1; cwd = n; return 0;
}
int vfs_mkdir(const char *name) {
    if (disk && ext4_mounted()) return ext4_mkdir(name);
    for (u32 i = 0; name[i]; ++i) if (name[i] == '/') return -1;
    if (!name[0] || eq(name, ".") || eq(name, "..") || child(cwd, name) != NONE) return -1;
    u8 n; for (n = 1; n < VFS_NODES && nodes[n].used; ++n) {} if (n == VFS_NODES) return -1;
    nodes[n].used = 1; nodes[n].parent = cwd; nodes[n].first_child = NONE; nodes[n].next = nodes[cwd].first_child;
    name_copy(nodes[n].name, name); nodes[cwd].first_child = n; return 0;
}
static void vfs_free_tree(u8 n) {
    for (u8 c = nodes[n].first_child; c != NONE;) {
        u8 next = nodes[c].next;
        vfs_free_tree(c);
        c = next;
    }
    nodes[n].used = 0;
    nodes[n].first_child = NONE;
}
int vfs_rm(const char *path, int recursive) {
    if (disk && ext4_mounted()) return ext4_rm(path, recursive);
    u8 n = resolve(path);
    if (n == NONE || n == 0 || n == cwd) return -1;
    if (nodes[n].first_child != NONE) {
        if (!recursive) return -2;
        vfs_free_tree(n);
    }
    u8 p = nodes[n].parent;
    if (nodes[p].first_child == n) {
        nodes[p].first_child = nodes[n].next;
    } else {
        for (u8 prev = nodes[p].first_child; prev != NONE; prev = nodes[prev].next) {
            if (nodes[prev].next == n) {
                nodes[prev].next = nodes[n].next;
                break;
            }
        }
    }
    nodes[n].used = 0;
    return 0;
}

