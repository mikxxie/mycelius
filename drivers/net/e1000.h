#ifndef DRIVERS_NET_E1000_H
#define DRIVERS_NET_E1000_H

#include "kernel.h"

void e1000_init(void);
void e1000_poll(void);
int e1000_send(const void *data, u32 len);

#endif /* DRIVERS_NET_E1000_H */