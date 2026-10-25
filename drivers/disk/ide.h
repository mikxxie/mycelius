#ifndef DRIVERS_DISK_IDE_H
#define DRIVERS_DISK_IDE_H

#include "kernel.h"

void ide_init(void);
int ide_read(u64 lba, void *buf, u32 sectors);
int ide_write(u64 lba, const void *buf, u32 sectors);

#endif /* DRIVERS_DISK_IDE_H */