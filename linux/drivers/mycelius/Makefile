# mycelius daemons - built as static aarch64 binaries for initramfs
# Part of the Linux kernel tree at linux/drivers/mycelius/

CC ?= aarch64-linux-gnu-gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11 -I.

SRCS := backboardd.c launchpad.c render.c
OBJS := $(SRCS:.c=.o)
PROGS := backboardd launchpad

.PHONY: all clean

all: $(PROGS)

backboardd: backboardd.c lp_proto.h
	$(CC) $(CFLAGS) -static -o $@ backboardd.c

launchpad: launchpad.c render.c lp_proto.h render.h
	$(CC) $(CFLAGS) -static -o $@ launchpad.c render.c

clean:
	rm -f $(PROGS) $(OBJS)

install: $(PROGS)
	install -m 0755 $(PROGS) ../../../rootfs/sbin/
