#ifndef _SYS_RESOURCE_H_
#define _SYS_RESOURCE_H_

#include <sys/types.h>

#define PRIO_PROCESS  0
#define PRIO_PGRP     1
#define PRIO_USER     2

int setpriority(int which, id_t who, int prio);

#endif
