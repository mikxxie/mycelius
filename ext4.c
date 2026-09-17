#include "kernel.h"

/* Freestanding helpers */
static u32 str_len(const char *s) { u32 n = 0; while (s[n]) ++n; return n; }
static int str_eq(const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
static void mem_set(void *p, u8 val, u32 n) { u8 *d = (u8 *)p; while (n--) *d++ = val; }
static void mem_cpy(void *dest, const void *src, u32 n) { u8 *d = (u8 *)dest; const u8 *s = (const u8 *)src; while (n--) *d++ = *s++; }
static int mem_cmp(const void *a, const void *b, u32 n) { const u8 *p1 = (const u8 *)a, *p2 = (const u8 *)b; while (n--) { if (*p1 != *p2) return (int)*p1 - (int)*p2; ++p1; ++p2; } return 0; }

static u32 le32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static u16 le16(const u8 *p) { return (u16)p[0] | ((u16)p[1] << 8); }
static void put_le32(u8 *p, u32 v) { p[0] = (u8)(v & 0xff); p[1] = (u8)((v >> 8) & 0xff); p[2] = (u8)((v >> 16) & 0xff); p[3] = (u8)((v >> 24) & 0xff); }
static void put_le16(u8 *p, u16 v) { p[0] = (u8)(v & 0xff); p[1] = (u8)((v >> 8) & 0xff); }

/* Ext4 CRC32C (Castagnoli) */
static u32 crc32c(const void *data, u32 len, u32 crc) {
    const u8 *p = (const u8 *)data;
    while (len--) {
        crc ^= *p++;
        for (int i = 0; i < 8; ++i)
            crc = (crc >> 1) ^ (0x82F63B78u * (crc & 1));
    }
    return crc;
}

#define EXT4_MAGIC 0xef53
#define EXT4_EXTENTS_FL 0x80000
#define EXT4_FT_REG_FILE 1
#define EXT4_FT_DIR 2
#define EXT4_FT_DIR_CSUM 0xDE

/* Static filesystem state */
static int s_mounted = 0;
static u32 s_inodes_count = 2048;
static u32 s_blocks_count = 8192;
static u32 s_first_data_block = 1;
static u32 s_inodes_per_group = 2048;
static u32 s_inode_size = 256;
static u32 s_csum_seed = 0;
static u32 s_block_bitmap_blk = 66;
static u32 s_inode_bitmap_blk = 82;
static u32 s_inode_table_blk = 98;
static u32 s_cwd_ino = 2;
static char s_cwd_path[128] = "/";

/* Dedicated static buffers to conserve stack on 16 MiB RAM system */
static u8 sector_buf[512] __attribute__((aligned(16)));
static u8 itable_buf[1024] __attribute__((aligned(16)));
static u8 blk_buf1[1024] __attribute__((aligned(16)));
static u8 blk_buf2[1024] __attribute__((aligned(16)));
static u8 blk_buf3[1024] __attribute__((aligned(16)));
static u8 blk_buf4[1024] __attribute__((aligned(16)));
static u8 inode_buf[256] __attribute__((aligned(16)));
static u8 parent_inobuf[256] __attribute__((aligned(16)));
static u8 child_inobuf[256] __attribute__((aligned(16)));
static u8 child_dirbuf[1024] __attribute__((aligned(16)));

static int read_1k(u32 fs_block, void *buf) {
    return virtio_blk_read((u64)fs_block * 2, buf) ||
           virtio_blk_read((u64)fs_block * 2 + 1, (u8 *)buf + 512);
}

static int write_1k(u32 fs_block, const void *buf) {
    return virtio_blk_write((u64)fs_block * 2, buf) ||
           virtio_blk_write((u64)fs_block * 2 + 1, (const u8 *)buf + 512);
}

static int ext4_read_inode(u32 ino, u8 *out_inode) {
    if (ino < 1 || ino > s_inodes_count) return -1;
    u32 idx = (ino - 1) % s_inodes_per_group;
    u32 byte_off = idx * s_inode_size;
    u32 blk = s_inode_table_blk + (byte_off / 1024);
    u32 off = byte_off % 1024;
    if (read_1k(blk, itable_buf)) return -1;
    mem_cpy(out_inode, itable_buf + off, s_inode_size);
    return 0;
}

static int ext4_write_inode(u32 ino, u8 *in_inode) {
    if (ino < 1 || ino > s_inodes_count) return -1;
    u16 extra = le16(in_inode + 128);
    in_inode[124] = 0; in_inode[125] = 0;
    in_inode[130] = 0; in_inode[131] = 0;
    u32 c = crc32c(&ino, 4, s_csum_seed);
    u32 gen = 0;
    c = crc32c(&gen, 4, c);
    c = crc32c(in_inode, s_inode_size, c);
    put_le16(in_inode + 124, (u16)(c & 0xffff));
    if (extra >= 4) {
        put_le16(in_inode + 130, (u16)((c >> 16) & 0xffff));
    }

    u32 idx = (ino - 1) % s_inodes_per_group;
    u32 byte_off = idx * s_inode_size;
    u32 blk = s_inode_table_blk + (byte_off / 1024);
    u32 off = byte_off % 1024;
    if (read_1k(blk, itable_buf)) return -1;
    mem_cpy(itable_buf + off, in_inode, s_inode_size);
    return write_1k(blk, itable_buf);
}

static u32 ext4_extent_bmap(const u8 *in_buf, u32 target_lblk) {
    const u8 *iblk = in_buf + 40;
    u16 magic = le16(iblk);
    if (magic != 0xf30a) return 0;
    u16 entries = le16(iblk + 2);
    u16 depth = le16(iblk + 6);
    if (depth == 0) {
        for (u16 i = 0; i < entries; ++i) {
            const u8 *ee = iblk + 12 + (u32)i * 12;
            u32 ee_block = le32(ee);
            u16 ee_len = le16(ee + 4);
            u32 ee_start = le32(ee + 8);
            if (target_lblk >= ee_block && target_lblk < ee_block + (u32)ee_len) {
                return ee_start + (target_lblk - ee_block);
            }
        }
    }
    return 0;
}

static int ext4_lookup(u32 dir_ino, const char *name, u32 *out_ino, u8 *out_type) {
    u8 inb[256];
    if (ext4_read_inode(dir_ino, inb)) return -1;
    u32 pblk = ext4_extent_bmap(inb, 0);
    if (!pblk) return -1;
    if (read_1k(pblk, blk_buf4)) return -1;
    u32 nlen = str_len(name);
    for (u32 pos = 0; pos + 8 <= 1024;) {
        u32 ino = le32(blk_buf4 + pos);
        u16 rec_len = le16(blk_buf4 + pos + 4);
        u8 name_len = blk_buf4[pos + 6];
        u8 file_type = blk_buf4[pos + 7];
        if (rec_len < 8 || pos + rec_len > 1024) break;
        if (ino && file_type != EXT4_FT_DIR_CSUM) {
            if (name_len == nlen && mem_cmp(blk_buf4 + pos + 8, name, nlen) == 0) {
                if (out_ino) *out_ino = ino;
                if (out_type) *out_type = file_type;
                return 0;
            }
        }
        pos += rec_len;
    }
    return -1;
}

static int ext4_resolve_path(const char *path, u32 *out_ino) {
    if (!path || !path[0]) {
        *out_ino = s_cwd_ino;
        return 0;
    }
    u32 curr = (*path == '/') ? 2 : s_cwd_ino;
    char piece[32];
    while (*path) {
        while (*path == '/') ++path;
        if (!*path) break;
        u32 len = 0;
        while (*path && *path != '/' && len < 31) piece[len++] = *path++;
        piece[len] = 0;
        if (str_eq(piece, ".")) continue;
        if (str_eq(piece, "..")) {
            u32 parent_ino = 0;
            if (ext4_lookup(curr, "..", &parent_ino, 0)) return -1;
            curr = parent_ino;
            continue;
        }
        u32 child_ino = 0;
        if (ext4_lookup(curr, piece, &child_ino, 0)) return -1;
        curr = child_ino;
    }
    *out_ino = curr;
    return 0;
}

static int ext4_mkdir_single(u32 parent_ino, const char *name) {
    if (!name || !name[0] || str_len(name) > 31) return -1;
    if (str_eq(name, ".") || str_eq(name, "..")) return -1;
    if (ext4_lookup(parent_ino, name, 0, 0) == 0) return -1;

    /* 1. Allocate free inode from inode bitmap (block 82) */
    if (read_1k(s_inode_bitmap_blk, blk_buf1)) return -1;
    u32 new_ino = 0;
    for (u32 i = 11; i < s_inodes_count; ++i) {
        if (!(blk_buf1[i / 8] & (1u << (i % 8)))) {
            blk_buf1[i / 8] |= (1u << (i % 8));
            new_ino = i + 1;
            break;
        }
    }
    if (!new_ino) return -1;
    if (write_1k(s_inode_bitmap_blk, blk_buf1)) return -1;

    /* 2. Allocate free data block from block bitmap (block 66) */
    if (read_1k(s_block_bitmap_blk, blk_buf2)) return -1;
    u32 new_blk = 0;
    for (u32 i = 0; i < s_blocks_count; ++i) {
        if (!(blk_buf2[i / 8] & (1u << (i % 8)))) {
            blk_buf2[i / 8] |= (1u << (i % 8));
            new_blk = i + s_first_data_block;
            break;
        }
    }
    if (!new_blk) return -1;
    if (write_1k(s_block_bitmap_blk, blk_buf2)) return -1;

    /* 3. Initialize new directory block */
    mem_set(blk_buf3, 0, 1024);
    /* Entry . */
    put_le32(blk_buf3, new_ino);
    put_le16(blk_buf3 + 4, 12);
    blk_buf3[6] = 1;
    blk_buf3[7] = EXT4_FT_DIR;
    blk_buf3[8] = '.';
    /* Entry .. */
    put_le32(blk_buf3 + 12, parent_ino);
    put_le16(blk_buf3 + 16, 1000);
    blk_buf3[18] = 2;
    blk_buf3[19] = EXT4_FT_DIR;
    blk_buf3[20] = '.';
    blk_buf3[21] = '.';
    /* Tail at 1012 */
    put_le32(blk_buf3 + 1012, 0);
    put_le16(blk_buf3 + 1016, 12);
    blk_buf3[1018] = 0;
    blk_buf3[1019] = EXT4_FT_DIR_CSUM;
    u32 c = crc32c(&new_ino, 4, s_csum_seed);
    u32 gen = 0;
    c = crc32c(&gen, 4, c);
    c = crc32c(blk_buf3, 1012, c);
    put_le32(blk_buf3 + 1020, c);
    if (write_1k(new_blk, blk_buf3)) return -1;

    /* 4. Initialize new inode */
    mem_set(inode_buf, 0, 256);
    put_le16(inode_buf, 0x41ed); /* mode: directory 0755 */
    put_le32(inode_buf + 4, 1024); /* size_lo */
    put_le32(inode_buf + 8, 0x6aa99c1e); /* atime */
    put_le32(inode_buf + 12, 0x6aa99c1e); /* ctime */
    put_le32(inode_buf + 16, 0x6aa99c1e); /* mtime */
    put_le16(inode_buf + 26, 2); /* links_count (. and parent) */
    put_le32(inode_buf + 28, 2); /* blocks_lo: 2 sectors */
    put_le32(inode_buf + 32, EXT4_EXTENTS_FL);
    /* Extent header at 40 */
    put_le16(inode_buf + 40, 0xf30a);
    put_le16(inode_buf + 42, 1);
    put_le16(inode_buf + 44, 4);
    put_le16(inode_buf + 46, 0);
    put_le32(inode_buf + 48, 0);
    /* Leaf extent at 52 */
    put_le32(inode_buf + 52, 0);
    put_le16(inode_buf + 56, 1);
    put_le16(inode_buf + 58, 0);
    put_le32(inode_buf + 60, new_blk);
    put_le16(inode_buf + 128, 32); /* extra isize */
    if (ext4_write_inode(new_ino, inode_buf)) return -1;

    /* 5. Insert directory entry into parent directory data block */
    if (ext4_read_inode(parent_ino, parent_inobuf)) return -1;
    u32 parent_pblk = ext4_extent_bmap(parent_inobuf, 0);
    if (!parent_pblk) return -1;
    if (read_1k(parent_pblk, blk_buf4)) return -1;

    u32 nlen = str_len(name);
    u16 needed = (8 + (u16)nlen + 3) & ~3;
    int inserted = 0;
    for (u32 pos = 0; pos + 8 <= 1012;) {
        u16 e_rec = le16(blk_buf4 + pos + 4);
        u8 e_name_len = blk_buf4[pos + 6];
        u8 e_ft = blk_buf4[pos + 7];
        if (e_rec < 8 || pos + e_rec > 1024) break;
        if (e_ft == EXT4_FT_DIR_CSUM) break;
        u16 actual = (8 + (u16)e_name_len + 3) & ~3;
        if (e_rec >= actual + needed) {
            u16 slack = e_rec - actual;
            put_le16(blk_buf4 + pos + 4, actual);
            u32 new_pos = pos + actual;
            put_le32(blk_buf4 + new_pos, new_ino);
            put_le16(blk_buf4 + new_pos + 4, slack);
            blk_buf4[new_pos + 6] = (u8)nlen;
            blk_buf4[new_pos + 7] = EXT4_FT_DIR;
            mem_cpy(blk_buf4 + new_pos + 8, name, nlen);
            inserted = 1;
            break;
        }
        pos += e_rec;
    }
    if (!inserted) return -1;

    /* Update parent directory tail checksum */
    c = crc32c(&parent_ino, 4, s_csum_seed);
    gen = 0;
    c = crc32c(&gen, 4, c);
    c = crc32c(blk_buf4, 1012, c);
    put_le32(blk_buf4 + 1020, c);
    if (write_1k(parent_pblk, blk_buf4)) return -1;

    /* 6. Increment parent inode links */
    u16 parent_links = le16(parent_inobuf + 26) + 1;
    put_le16(parent_inobuf + 26, parent_links);
    if (ext4_write_inode(parent_ino, parent_inobuf)) return -1;

    /* 7. Update Group Descriptor (block 2) */
    if (read_1k(2, blk_buf3)) return -1;
    put_le16(blk_buf3 + 12, le16(blk_buf3 + 12) - 1);
    put_le16(blk_buf3 + 14, le16(blk_buf3 + 14) - 1);
    put_le16(blk_buf3 + 16, le16(blk_buf3 + 16) + 1);
    put_le16(blk_buf3 + 0x1c, le16(blk_buf3 + 0x1c) - 1);
    u32 bb_c = crc32c(blk_buf2, 1024, s_csum_seed);
    put_le16(blk_buf3 + 0x18, (u16)(bb_c & 0xffff));
    put_le16(blk_buf3 + 0x38, (u16)((bb_c >> 16) & 0xffff));
    u32 ib_c = crc32c(blk_buf1, 256, s_csum_seed);
    put_le16(blk_buf3 + 0x1a, (u16)(ib_c & 0xffff));
    put_le16(blk_buf3 + 0x3a, (u16)((ib_c >> 16) & 0xffff));
    blk_buf3[0x1e] = 0; blk_buf3[0x1f] = 0;
    u32 zero_group = 0;
    u32 gd_c = crc32c(&zero_group, 4, s_csum_seed);
    u16 gd_csum = (u16)crc32c(blk_buf3, 64, gd_c);
    put_le16(blk_buf3 + 0x1e, gd_csum);
    if (write_1k(2, blk_buf3)) return -1;

    /* 8. Update Superblock (block 1) */
    if (read_1k(1, blk_buf3)) return -1;
    put_le32(blk_buf3 + 12, le32(blk_buf3 + 12) - 1);
    put_le32(blk_buf3 + 16, le32(blk_buf3 + 16) - 1);
    put_le32(blk_buf3 + 0x3fc, 0);
    u32 sb_c = crc32c(blk_buf3, 1020, 0xffffffff);
    put_le32(blk_buf3 + 0x3fc, sb_c);
    if (write_1k(1, blk_buf3)) return -1;

    virtio_blk_flush();
    return 0;
}

int ext4_probe(void) {
    if (virtio_blk_read(2, sector_buf)) return -1;
    return sector_buf[0x38] == 0x53 && sector_buf[0x39] == 0xef ? 0 : -1;
}

int ext4_mount(void) {
    if (ext4_probe()) return -1;
    if (read_1k(1, blk_buf3)) return -1;
    s_inodes_count = le32(blk_buf3);
    s_blocks_count = le32(blk_buf3 + 4);
    s_first_data_block = le32(blk_buf3 + 20);
    s_inodes_per_group = le32(blk_buf3 + 40);
    s_inode_size = le16(blk_buf3 + 0x58);
    if (s_inode_size < 128 || s_inode_size > 512) return -1;
    s_csum_seed = crc32c(blk_buf3 + 0x68, 16, 0xffffffff);

    if (read_1k(2, blk_buf3)) return -1;
    s_block_bitmap_blk = le32(blk_buf3);
    s_inode_bitmap_blk = le32(blk_buf3 + 4);
    s_inode_table_blk = le32(blk_buf3 + 8);

    s_cwd_ino = 2;
    s_cwd_path[0] = '/';
    s_cwd_path[1] = 0;
    s_mounted = 1;
    return 0;
}

int ext4_mounted(void) {
    return s_mounted;
}

static int ext4_ls_dir(u32 dir_ino) {
    u8 inb[256];
    if (ext4_read_inode(dir_ino, inb)) return -1;
    u32 pblk = ext4_extent_bmap(inb, 0);
    if (!pblk) return -1;
    if (read_1k(pblk, blk_buf4)) return -1;
    for (u32 pos = 0; pos + 8 <= 1024;) {
        u32 ino = le32(blk_buf4 + pos);
        u16 len = le16(blk_buf4 + pos + 4);
        u8 name_len = blk_buf4[pos + 6];
        u8 ft = blk_buf4[pos + 7];
        if (len < 8 || pos + len > 1024 || name_len > len - 8) break;
        if (ino && name_len && ft != EXT4_FT_DIR_CSUM &&
            !(name_len == 1 && blk_buf4[pos + 8] == '.') &&
            !(name_len == 2 && blk_buf4[pos + 8] == '.' && blk_buf4[pos + 9] == '.')) {
            char name[32];
            u32 n = name_len < 31 ? name_len : 31;
            for (u32 i = 0; i < n; ++i) name[i] = (char)blk_buf4[pos + 8 + i];
            name[n] = 0;
            uart_puts(name);
            if (ft == EXT4_FT_DIR) uart_puts("/  ");
            else uart_puts("  ");
        }
        pos += len;
    }
    uart_puts("\r\n");
    return 0;
}

int ext4_ls_root(void) {
    return ext4_ls_dir(2);
}

int ext4_ls(void) {
    return ext4_ls_dir(s_cwd_ino);
}

int ext4_ls_path(const char *path) {
    if (!path || !path[0]) return ext4_ls();
    u32 target = 0;
    if (ext4_resolve_path(path, &target)) return -1;
    return ext4_ls_dir(target);
}

void ext4_pwd(void) {
    uart_puts(s_cwd_path);
    uart_puts("\r\n");
}

int ext4_cd(const char *path) {
    if (!path || !path[0]) return 0;
    u32 target_ino = 0;
    if (ext4_resolve_path(path, &target_ino)) return -1;
    u8 inb[256];
    if (ext4_read_inode(target_ino, inb)) return -1;
    u16 mode = le16(inb);
    if ((mode & 0xf000) != 0x4000) return -1;

    char new_path[128];
    u32 np = 0;
    if (path[0] == '/') {
        new_path[np++] = '/';
        new_path[np] = 0;
    } else {
        while (s_cwd_path[np]) { new_path[np] = s_cwd_path[np]; ++np; }
        new_path[np] = 0;
    }
    const char *p = path;
    char piece[32];
    while (*p) {
        while (*p == '/') ++p;
        if (!*p) break;
        u32 len = 0;
        while (*p && *p != '/' && len < 31) piece[len++] = *p++;
        piece[len] = 0;
        if (str_eq(piece, ".")) continue;
        if (str_eq(piece, "..")) {
            if (np > 1) {
                while (np > 1 && new_path[np - 1] != '/') --np;
                if (np > 1) --np;
                new_path[np] = 0;
            }
            continue;
        }
        if (np == 0 || new_path[np - 1] != '/') new_path[np++] = '/';
        for (u32 i = 0; piece[i] && np < 126; ++i) new_path[np++] = piece[i];
        new_path[np] = 0;
    }
    if (np == 0) { new_path[0] = '/'; new_path[1] = 0; np = 1; }
    for (u32 i = 0; i <= np; ++i) s_cwd_path[i] = new_path[i];
    s_cwd_ino = target_ino;
    return 0;
}

int ext4_mkdir(const char *path) {
    if (!path || !path[0]) return -1;
    u32 curr = (*path == '/') ? 2 : s_cwd_ino;
    char piece[32];
    int made_any = 0;
    while (*path) {
        while (*path == '/') ++path;
        if (!*path) break;
        u32 len = 0;
        while (*path && *path != '/' && len < 31) piece[len++] = *path++;
        piece[len] = 0;
        if (str_eq(piece, ".")) continue;
        if (str_eq(piece, "..")) {
            u32 parent_ino = 0;
            if (ext4_lookup(curr, "..", &parent_ino, 0)) return -1;
            curr = parent_ino;
            continue;
        }
        u32 child_ino = 0;
        u8 child_type = 0;
        if (ext4_lookup(curr, piece, &child_ino, &child_type) == 0) {
            if (child_type != EXT4_FT_DIR) return -1;
            curr = child_ino;
        } else {
            if (ext4_mkdir_single(curr, piece) != 0) return -1;
            if (ext4_lookup(curr, piece, &child_ino, &child_type) != 0) return -1;
            curr = child_ino;
            made_any = 1;
        }
    }
    return made_any ? 0 : -1;
}

/* Create a new regular file and write its data (single 1024-byte block) */
static int ext4_write_create(u32 parent_ino, const char *name, const u8 *data, u32 len) {
    if (len > 1024) return -1;

    /* 1. Allocate free inode from inode bitmap (block 82) */
    if (read_1k(s_inode_bitmap_blk, blk_buf1)) return -1;
    u32 new_ino = 0;
    for (u32 i = 11; i < s_inodes_count; ++i) {
        if (!(blk_buf1[i / 8] & (1u << (i % 8)))) {
            blk_buf1[i / 8] |= (1u << (i % 8));
            new_ino = i + 1;
            break;
        }
    }
    if (!new_ino) return -1;
    if (write_1k(s_inode_bitmap_blk, blk_buf1)) return -1;

    /* 2. Allocate free data block from block bitmap (block 66) */
    if (read_1k(s_block_bitmap_blk, blk_buf2)) return -1;
    u32 new_blk = 0;
    for (u32 i = 0; i < s_blocks_count; ++i) {
        if (!(blk_buf2[i / 8] & (1u << (i % 8)))) {
            blk_buf2[i / 8] |= (1u << (i % 8));
            new_blk = i + s_first_data_block;
            break;
        }
    }
    if (!new_blk) return -1;
    if (write_1k(s_block_bitmap_blk, blk_buf2)) return -1;

    /* 3. Write file data into the new block */
    mem_set(blk_buf3, 0, 1024);
    mem_cpy(blk_buf3, data, len);
    if (write_1k(new_blk, blk_buf3)) return -1;

    /* 4. Initialize new inode (regular file 0644) */
    mem_set(inode_buf, 0, 256);
    put_le16(inode_buf, 0x81a4); /* mode: regular 0644 */
    put_le32(inode_buf + 4, len); /* size_lo */
    put_le32(inode_buf + 8, 0x6aa99c1e); /* atime */
    put_le32(inode_buf + 12, 0x6aa99c1e); /* ctime */
    put_le32(inode_buf + 16, 0x6aa99c1e); /* mtime */
    put_le16(inode_buf + 26, 1); /* links_count */
    put_le32(inode_buf + 28, 2); /* blocks_lo: 2 sectors */
    put_le32(inode_buf + 32, EXT4_EXTENTS_FL);
    /* Extent header at 40 */
    put_le16(inode_buf + 40, 0xf30a);
    put_le16(inode_buf + 42, 1);
    put_le16(inode_buf + 44, 4);
    put_le16(inode_buf + 46, 0);
    put_le32(inode_buf + 48, 0);
    /* Leaf extent at 52 */
    put_le32(inode_buf + 52, 0);
    put_le16(inode_buf + 56, 1);
    put_le16(inode_buf + 58, 0);
    put_le32(inode_buf + 60, new_blk);
    put_le16(inode_buf + 128, 32); /* extra isize */
    if (ext4_write_inode(new_ino, inode_buf)) return -1;

    /* 5. Insert directory entry into parent directory data block */
    if (ext4_read_inode(parent_ino, parent_inobuf)) return -1;
    u32 parent_pblk = ext4_extent_bmap(parent_inobuf, 0);
    if (!parent_pblk) return -1;
    if (read_1k(parent_pblk, blk_buf4)) return -1;

    u32 nlen = str_len(name);
    u16 needed = (8 + (u16)nlen + 3) & ~3;
    int inserted = 0;
    for (u32 pos = 0; pos + 8 <= 1012;) {
        u16 e_rec = le16(blk_buf4 + pos + 4);
        u8 e_name_len = blk_buf4[pos + 6];
        u8 e_ft = blk_buf4[pos + 7];
        if (e_rec < 8 || pos + e_rec > 1024) break;
        if (e_ft == EXT4_FT_DIR_CSUM) break;
        u16 actual = (8 + (u16)e_name_len + 3) & ~3;
        if (e_rec >= actual + needed) {
            u16 slack = e_rec - actual;
            put_le16(blk_buf4 + pos + 4, actual);
            u32 new_pos = pos + actual;
            put_le32(blk_buf4 + new_pos, new_ino);
            put_le16(blk_buf4 + new_pos + 4, slack);
            blk_buf4[new_pos + 6] = (u8)nlen;
            blk_buf4[new_pos + 7] = EXT4_FT_REG_FILE;
            mem_cpy(blk_buf4 + new_pos + 8, name, nlen);
            inserted = 1;
            break;
        }
        pos += e_rec;
    }
    if (!inserted) return -1;

    /* Update parent directory tail checksum */
    u32 c = crc32c(&parent_ino, 4, s_csum_seed);
    u32 gen = 0;
    c = crc32c(&gen, 4, c);
    c = crc32c(blk_buf4, 1012, c);
    put_le32(blk_buf4 + 1020, c);
    if (write_1k(parent_pblk, blk_buf4)) return -1;

    /* 6. Update Group Descriptor (block 2) */
    if (read_1k(2, blk_buf3)) return -1;
    put_le16(blk_buf3 + 12, le16(blk_buf3 + 12) - 1);
    put_le16(blk_buf3 + 14, le16(blk_buf3 + 14) - 1);
    u32 bb_c = crc32c(blk_buf2, 1024, s_csum_seed);
    put_le16(blk_buf3 + 0x18, (u16)(bb_c & 0xffff));
    put_le16(blk_buf3 + 0x38, (u16)((bb_c >> 16) & 0xffff));
    u32 ib_c = crc32c(blk_buf1, 256, s_csum_seed);
    put_le16(blk_buf3 + 0x1a, (u16)(ib_c & 0xffff));
    put_le16(blk_buf3 + 0x3a, (u16)((ib_c >> 16) & 0xffff));
    blk_buf3[0x1e] = 0; blk_buf3[0x1f] = 0;
    u32 zero_group = 0;
    u32 gd_c = crc32c(&zero_group, 4, s_csum_seed);
    u16 gd_csum = (u16)crc32c(blk_buf3, 64, gd_c);
    put_le16(blk_buf3 + 0x1e, gd_csum);
    if (write_1k(2, blk_buf3)) return -1;

    /* 7. Update Superblock (block 1) */
    if (read_1k(1, blk_buf3)) return -1;
    put_le32(blk_buf3 + 12, le32(blk_buf3 + 12) - 1);
    put_le32(blk_buf3 + 16, le32(blk_buf3 + 16) - 1);
    put_le32(blk_buf3 + 0x3fc, 0);
    u32 sb_c = crc32c(blk_buf3, 1020, 0xffffffff);
    put_le32(blk_buf3 + 0x3fc, sb_c);
    if (write_1k(1, blk_buf3)) return -1;

    virtio_blk_flush();
    return 0;
}

/* Overwrite an existing regular file in place */
static int ext4_write_overwrite(u32 parent_ino, u32 target_ino, const u8 *data, u32 len) {
    (void)parent_ino;
    if (len > 1024) return -1;
    u8 t_inobuf[256];
    if (ext4_read_inode(target_ino, t_inobuf)) return -1;
    if ((le16(t_inobuf) & 0xf000) != 0x8000) return -1; /* must be regular file */
    u32 pblk = ext4_extent_bmap(t_inobuf, 0);
    if (!pblk) return -1;

    mem_set(blk_buf3, 0, 1024);
    mem_cpy(blk_buf3, data, len);
    if (write_1k(pblk, blk_buf3)) return -1;

    put_le32(t_inobuf + 4, len); /* size_lo */
    put_le32(t_inobuf + 16, 0x6aa99c1e); /* mtime */
    put_le32(t_inobuf + 28, 2); /* blocks_lo: 2 sectors */
    if (ext4_write_inode(target_ino, t_inobuf)) return -1;

    virtio_blk_flush();
    return 0;
}

int ext4_write_file(const char *path, const u8 *data, u32 len) {
    if (!path || !path[0] || !data || len > 1024) return -1;
    char pbuf[128];
    u32 plen = 0;
    while (path[plen] && plen < 127) { pbuf[plen] = path[plen]; plen++; }
    while (plen > 1 && pbuf[plen - 1] == '/') --plen;
    pbuf[plen] = 0;

    if (str_eq(pbuf, "/") || str_eq(pbuf, ".") || str_eq(pbuf, "..")) return -1;

    char parent_path[128];
    char target_name[32];
    int last_slash = -1;
    for (int i = (int)plen - 1; i >= 0; --i) {
        if (pbuf[i] == '/') { last_slash = i; break; }
    }

    if (last_slash < 0) {
        parent_path[0] = 0;
        u32 i = 0;
        while (pbuf[i] && i < 31) { target_name[i] = pbuf[i]; ++i; }
        target_name[i] = 0;
    } else if (last_slash == 0) {
        parent_path[0] = '/';
        parent_path[1] = 0;
        u32 i = 0;
        while (pbuf[last_slash + 1 + i] && i < 31) { target_name[i] = pbuf[last_slash + 1 + i]; ++i; }
        target_name[i] = 0;
    } else {
        for (int i = 0; i < last_slash; ++i) parent_path[i] = pbuf[i];
        parent_path[last_slash] = 0;
        u32 i = 0;
        while (pbuf[last_slash + 1 + i] && i < 31) { target_name[i] = pbuf[last_slash + 1 + i]; ++i; }
        target_name[i] = 0;
    }

    if (!target_name[0] || str_eq(target_name, ".") || str_eq(target_name, "..")) return -1;

    u32 parent_ino = 0;
    if (ext4_resolve_path(parent_path, &parent_ino)) return -1;

    u32 target_ino = 0;
    u8 target_type = 0;
    if (ext4_lookup(parent_ino, target_name, &target_ino, &target_type) == 0) {
        if (ext4_write_overwrite(parent_ino, target_ino, data, len)) return -1;
    } else {
        if (ext4_write_create(parent_ino, target_name, data, len)) return -1;
    }
    return 0;
}

static int ext4_rm_single(u32 parent_ino, const char *name, u32 target_ino, u8 target_type) {
    if (target_ino <= 11 || target_ino == s_cwd_ino) return -1;

    /* 1. Read target inode to get its data block */
    if (ext4_read_inode(target_ino, child_inobuf)) return -1;
    u32 pblk = ext4_extent_bmap(child_inobuf, 0);

    /* 2. Free data block from block bitmap (block 66) */
    if (pblk) {
        if (read_1k(s_block_bitmap_blk, blk_buf2)) return -1;
        u32 bit = pblk - s_first_data_block;
        blk_buf2[bit / 8] &= (u8)~(1u << (bit % 8));
        if (write_1k(s_block_bitmap_blk, blk_buf2)) return -1;
    }

    /* 3. Free inode from inode bitmap (block 82) */
    if (read_1k(s_inode_bitmap_blk, blk_buf1)) return -1;
    u32 ibit = target_ino - 1;
    blk_buf1[ibit / 8] &= (u8)~(1u << (ibit % 8));
    if (write_1k(s_inode_bitmap_blk, blk_buf1)) return -1;

    /* 4. Zero the inode in inode table and set dtime */
    mem_set(inode_buf, 0, 256);
    put_le32(inode_buf + 20, 0x6aa99c1e); /* i_dtime */
    if (ext4_write_inode(target_ino, inode_buf)) return -1;

    /* 5. Unlink entry from parent directory data block */
    if (ext4_read_inode(parent_ino, parent_inobuf)) return -1;
    u32 parent_pblk = ext4_extent_bmap(parent_inobuf, 0);
    if (!parent_pblk) return -1;
    if (read_1k(parent_pblk, blk_buf4)) return -1;

    u32 prev_pos = 0;
    int unlinked = 0;
    u32 nlen = str_len(name);
    for (u32 pos = 0; pos + 8 <= 1012;) {
        u32 e_ino = le32(blk_buf4 + pos);
        u16 e_rec = le16(blk_buf4 + pos + 4);
        u8 e_name_len = blk_buf4[pos + 6];
        u8 e_ft = blk_buf4[pos + 7];
        if (e_rec < 8 || pos + e_rec > 1024) break;
        if (e_ft == EXT4_FT_DIR_CSUM) break;
        if (e_ino == target_ino && (u32)e_name_len == nlen &&
            mem_cmp(blk_buf4 + pos + 8, name, nlen) == 0) {
            if (pos > 0) {
                u16 prev_rec = le16(blk_buf4 + prev_pos + 4);
                put_le16(blk_buf4 + prev_pos + 4, prev_rec + e_rec);
            } else {
                put_le32(blk_buf4 + pos, 0);
            }
            unlinked = 1;
            break;
        }
        prev_pos = pos;
        pos += e_rec;
    }
    if (!unlinked) return -1;

    /* Update parent directory tail checksum */
    u32 c = crc32c(&parent_ino, 4, s_csum_seed);
    u32 gen = 0;
    c = crc32c(&gen, 4, c);
    c = crc32c(blk_buf4, 1012, c);
    put_le32(blk_buf4 + 1020, c);
    if (write_1k(parent_pblk, blk_buf4)) return -1;

    /* 6. Decrement parent links if target was directory */
    if (target_type == EXT4_FT_DIR) {
        u16 links = le16(parent_inobuf + 26);
        if (links > 2) links--;
        put_le16(parent_inobuf + 26, links);
        if (ext4_write_inode(parent_ino, parent_inobuf)) return -1;
    }

    /* 7. Update Group Descriptor (block 2) */
    if (read_1k(2, blk_buf3)) return -1;
    if (pblk) put_le16(blk_buf3 + 12, le16(blk_buf3 + 12) + 1);
    put_le16(blk_buf3 + 14, le16(blk_buf3 + 14) + 1);
    if (target_type == EXT4_FT_DIR) {
        put_le16(blk_buf3 + 16, le16(blk_buf3 + 16) - 1);
    }
    u32 bb_c = crc32c(blk_buf2, 1024, s_csum_seed);
    put_le16(blk_buf3 + 0x18, (u16)(bb_c & 0xffff));
    put_le16(blk_buf3 + 0x38, (u16)((bb_c >> 16) & 0xffff));
    u32 ib_c = crc32c(blk_buf1, 256, s_csum_seed);
    put_le16(blk_buf3 + 0x1a, (u16)(ib_c & 0xffff));
    put_le16(blk_buf3 + 0x3a, (u16)((ib_c >> 16) & 0xffff));
    blk_buf3[0x1e] = 0; blk_buf3[0x1f] = 0;
    u32 zero_group = 0;
    u32 gd_c = crc32c(&zero_group, 4, s_csum_seed);
    u16 gd_csum = (u16)crc32c(blk_buf3, 64, gd_c);
    put_le16(blk_buf3 + 0x1e, gd_csum);
    if (write_1k(2, blk_buf3)) return -1;

    /* 8. Update Superblock (block 1) */
    if (read_1k(1, blk_buf3)) return -1;
    if (pblk) put_le32(blk_buf3 + 12, le32(blk_buf3 + 12) + 1);
    put_le32(blk_buf3 + 16, le32(blk_buf3 + 16) + 1);
    put_le32(blk_buf3 + 0x3fc, 0);
    u32 sb_c = crc32c(blk_buf3, 1020, 0xffffffff);
    put_le32(blk_buf3 + 0x3fc, sb_c);
    if (write_1k(1, blk_buf3)) return -1;

    virtio_blk_flush();
    return 0;
}

struct rm_child {
    u32 ino;
    u8 type;
    char name[32];
};

static int ext4_rm_tree(u32 parent_ino, const char *name, u32 target_ino, u8 target_type, int recursive) {
    if (target_type == EXT4_FT_DIR) {
        if (!recursive) return -2;
        u8 tinob[256];
        if (ext4_read_inode(target_ino, tinob)) return -1;
        u32 tpblk = ext4_extent_bmap(tinob, 0);
        if (!tpblk) return -1;
        if (read_1k(tpblk, child_dirbuf)) return -1;

        struct rm_child children[16];
        u32 ccount = 0;
        for (u32 pos = 0; pos + 8 <= 1012 && ccount < 16;) {
            u32 c_ino = le32(child_dirbuf + pos);
            u16 c_rec = le16(child_dirbuf + pos + 4);
            u8 c_len = child_dirbuf[pos + 6];
            u8 c_ft = child_dirbuf[pos + 7];
            if (c_rec < 8 || pos + c_rec > 1024) break;
            if (c_ft == EXT4_FT_DIR_CSUM) break;
            if (c_ino && c_len &&
                !(c_len == 1 && child_dirbuf[pos + 8] == '.') &&
                !(c_len == 2 && child_dirbuf[pos + 8] == '.' && child_dirbuf[pos + 9] == '.')) {
                children[ccount].ino = c_ino;
                children[ccount].type = c_ft;
                u32 copylen = c_len < 31 ? c_len : 31;
                for (u32 j = 0; j < copylen; ++j) children[ccount].name[j] = (char)child_dirbuf[pos + 8 + j];
                children[ccount].name[copylen] = 0;
                ccount++;
            }
            pos += c_rec;
        }

        for (u32 i = 0; i < ccount; ++i) {
            int r = ext4_rm_tree(target_ino, children[i].name, children[i].ino, children[i].type, 1);
            if (r) return r;
        }
    }
    return ext4_rm_single(parent_ino, name, target_ino, target_type);
}

int ext4_rm(const char *path, int recursive) {
    if (!path || !path[0]) return -1;
    char pbuf[128];
    u32 plen = 0;
    while (path[plen] && plen < 127) { pbuf[plen] = path[plen]; plen++; }
    while (plen > 1 && pbuf[plen - 1] == '/') --plen;
    pbuf[plen] = 0;

    if (str_eq(pbuf, "/") || str_eq(pbuf, ".") || str_eq(pbuf, "..")) return -1;

    char parent_path[128];
    char target_name[32];
    int last_slash = -1;
    for (int i = (int)plen - 1; i >= 0; --i) {
        if (pbuf[i] == '/') { last_slash = i; break; }
    }

    if (last_slash < 0) {
        parent_path[0] = 0;
        u32 i = 0;
        while (pbuf[i] && i < 31) { target_name[i] = pbuf[i]; ++i; }
        target_name[i] = 0;
    } else if (last_slash == 0) {
        parent_path[0] = '/';
        parent_path[1] = 0;
        u32 i = 0;
        while (pbuf[last_slash + 1 + i] && i < 31) { target_name[i] = pbuf[last_slash + 1 + i]; ++i; }
        target_name[i] = 0;
    } else {
        for (int i = 0; i < last_slash; ++i) parent_path[i] = pbuf[i];
        parent_path[last_slash] = 0;
        u32 i = 0;
        while (pbuf[last_slash + 1 + i] && i < 31) { target_name[i] = pbuf[last_slash + 1 + i]; ++i; }
        target_name[i] = 0;
    }

    if (!target_name[0] || str_eq(target_name, ".") || str_eq(target_name, "..")) return -1;

    u32 parent_ino = 0;
    if (ext4_resolve_path(parent_path, &parent_ino)) return -1;

    u32 target_ino = 0;
    u8 target_type = 0;
    if (ext4_lookup(parent_ino, target_name, &target_ino, &target_type)) return -1;

    if (target_ino <= 11 || target_ino == s_cwd_ino) return -1;

    /* Prevent deleting an ancestor of cwd */
    u32 check = s_cwd_ino;
    while (check > 2) {
        if (check == target_ino) return -1;
        u32 parent = 0;
        if (ext4_lookup(check, "..", &parent, 0)) break;
        check = parent;
    }

    return ext4_rm_tree(parent_ino, target_name, target_ino, target_type, recursive);
}