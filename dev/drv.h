#pragma once
#include <stdint.h>
#include <stddef.h>

/* Mycelius driver interface - public header for EL0 driver processes.
 *
 * Drivers run as isolated EL0 tasks. They own their MMIO window and IPC
 * page, and communicate with the kernel via synchronous IPC syscalls.
 */

/* Driver types */
#define MYCELIUS_DRV_TYPE_NET   1
#define MYCELIUS_DRV_TYPE_BLK   2

/* Driver status codes */
#define MYCELIUS_DRV_OK         0
#define MYCELIUS_DRV_ERR_IO    -1
#define MYCELIUS_DRV_ERR_INVAL -2

/* Driver registration info. Filled by driver at EL0 init. */
struct mycelius_driver_info {
    uint32_t type;       /* driver type (MYCELIUS_DRV_TYPE_*) */
    uint32_t mmio_base;  /* MMIO base address (physical) */
    uint32_t mmio_size;  /* MMIO size in bytes */
    uint32_t irq;        /* assigned IRQ, or 0 if polled */
};

/* Register this driver with the kernel. Returns 0 on success. */
int mycelius_driver_register(const struct mycelius_driver_info *info);

/* Unregister this driver. Returns 0 on success. */
int mycelius_driver_unregister(void);

/* Read from driver MMIO space. Returns the 32-bit value read. */
uint32_t mycelius_dev_read32(u64 mmio_base, u32 offset);

/* Write to driver MMIO space. Returns 0 on success. */
int mycelius_dev_write32(u64 mmio_base, u32 offset, u32 value);

/* Read 32-bit value from MMIO at offset, return via pointer. */
int mycelius_dev_read32_sync(u64 mmio_base, u32 offset, u32 *val_out);
