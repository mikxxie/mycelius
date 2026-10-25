#include "kernel.h"

/* IDE/PATA storage fallback driver.
 * Based on FreeBSD /sys/dev/ata/ata-pci.c but simplified for PIO access.
 * Supports standard IDE command block registers (0x1F0-0x1F7) and
 * control block (0x3F6).
 *
 * Register offsets from primary channel base (0x1F0):
 *  DATA     0x000 - Data register (read/write sector data)
 *  ERROR    0x001 - Error register (read)
 *  FEATURES 0x001 - Features register (write)
 *  SECCNT   0x002 - Sector count register
 *  SECNUM   0x003 - Sector number register (LBA bits 0-7)
 *  CYLLOW   0x004 - Cylinder low register (LBA bits 8-15)
 *  CYLHIGH  0x005 - Cylinder high register (LBA bits 16-23)
 *  DH       0x006 - Drive/Head register (LBA bits 24-27, drive select)
 *  CMD      0x007 - Command register (write) / Status register (read)
 *  ALTSTAT  0x206 - Alternate status register (read)
 *  DEVCTL   0x206 - Device control register (write)
 */

#define IDE_DATA      0x1F0
#define IDE_ERROR     0x1F1
#define IDE_FEATURES  0x1F1
#define IDE_SECCNT    0x1F2
#define IDE_SECNUM    0x1F3
#define IDE_CYLLOW    0x1F4
#define IDE_CYLHIGH   0x1F5
#define IDE_DH        0x1F6
#define IDE_CMD       0x1F7
#define IDE_STATUS    0x1F7
#define IDE_ALTSTAT   0x3F6
#define IDE_DEVCTL    0x3F6

/* IDE commands */
#define IDE_CMD_READ    0x20   /* Read sectors */
#define IDE_CMD_WRITE   0x30   /* Write sectors */
#define IDE_CMD_IDENT   0xEC   /* Identify drive */
#define IDE_CMD_CACHE   0xE7   /* Flush cache */

/* Status register bits */
#define IDE_STAT_BSY    0x80   /* Controller busy */
#define IDE_STAT_DRDY   0x40   /* Drive ready */
#define IDE_STAT_DF     0x20   /* Drive fault */
#define IDE_STAT_DSC    0x10   /* Device seek complete */
#define IDE_STAT_DRQ    0x08   /* Data request ready */
#define IDE_STAT_CORR   0x04   /* Corrected data */
#define IDE_STAT_IDX    0x02   /* Index */
#define IDE_STAT_ERR    0x01   /* Error */

/* Drive select */
#define IDE_DH_MASTER   0xA0   /* Master drive, LBA mode */
#define IDE_DH_SLAVE    0xB0   /* Slave drive, LBA mode */

/* I/O port access (bare metal, no privilege issues on AArch64 EL1) */
static inline void outb(u16 port, u8 val) {
    asm volatile ("str %w1, [%0]" : : "r"((u64)port), "r"((u64)val) : "memory");
}

static inline u8 inb(u16 port) {
    u8 val;
    asm volatile ("ldr %w0, [%1]" : "=r"(val) : "r"((u64)port) : "memory");
    return val;
}

static inline void wait_400ns(void) {
    /* Read alternate status register 4 times (~400ns delay) */
    inb(IDE_ALTSTAT);
    inb(IDE_ALTSTAT);
    inb(IDE_ALTSTAT);
    inb(IDE_ALTSTAT);
}

static int ide_wait_ready(void) {
    int timeout = 1000000;
    while (timeout--) {
        u8 status = inb(IDE_STATUS);
        if (!(status & IDE_STAT_BSY)) {
            if (status & IDE_STAT_DRDY) return 0;
        }
    }
    return -1;
}

static int ide_wait_drq(void) {
    int timeout = 1000000;
    while (timeout--) {
        u8 status = inb(IDE_STATUS);
        if (!(status & IDE_STAT_BSY)) {
            if (status & IDE_STAT_DRQ) return 0;
        }
    }
    return -1;
}

void ide_init(void) {
    /* Select master drive */
    outb(IDE_DH, IDE_DH_MASTER);
    wait_400ns();

    /* Identify drive */
    outb(IDE_CMD, IDE_CMD_IDENT);
    if (ide_wait_drq() != 0) {
        uart_puts("ide: drive not ready\r\n");
        return;
    }

    /* Read identify data (256 words = 512 bytes) */
    for (int i = 0; i < 256; i++) {
        u16 data = inb(IDE_DATA) | (inb(IDE_DATA) << 8);
        (void)data;
    }

    uart_puts("ide: master drive initialized\r\n");
}

int ide_read(u64 lba, void *buf, u32 sectors) {
    u16 *buf16 = (u16 *)buf;
    u32 total_words = sectors * 256;

    /* Select drive and LBA */
    outb(IDE_DH, IDE_DH_MASTER | ((u8)((lba >> 24) & 0x0F)));
    wait_400ns();

    outb(IDE_FEATURES, 0);
    outb(IDE_SECCNT, (u8)sectors);
    outb(IDE_SECNUM, (u8)lba);
    outb(IDE_CYLLOW, (u8)(lba >> 8));
    outb(IDE_CYLHIGH, (u8)(lba >> 16));
    outb(IDE_CMD, IDE_CMD_READ);

    for (u32 s = 0; s < sectors; s++) {
        if (ide_wait_drq() != 0) return -1;
        for (int i = 0; i < 256; i++) {
            buf16[s * 256 + i] = inb(IDE_DATA) | (inb(IDE_DATA) << 8);
        }
    }

    return 0;
}

int ide_write(u64 lba, const void *buf, u32 sectors) {
    u16 *buf16 = (u16 *)buf;

    /* Select drive and LBA */
    outb(IDE_DH, IDE_DH_MASTER | ((u8)((lba >> 24) & 0x0F)));
    wait_400ns();

    outb(IDE_FEATURES, 0);
    outb(IDE_SECCNT, (u8)sectors);
    outb(IDE_SECNUM, (u8)lba);
    outb(IDE_CYLLOW, (u8)(lba >> 8));
    outb(IDE_CYLHIGH, (u8)(lba >> 16));
    outb(IDE_CMD, IDE_CMD_WRITE);

    for (u32 s = 0; s < sectors; s++) {
        if (ide_wait_drq() != 0) return -1;
        for (int i = 0; i < 256; i++) {
            outb(IDE_DATA, buf16[s * 256 + i] & 0xFF);
            outb(IDE_DATA, buf16[s * 256 + i] >> 8);
        }
        if (ide_wait_ready() != 0) return -1;
    }

    return 0;
}