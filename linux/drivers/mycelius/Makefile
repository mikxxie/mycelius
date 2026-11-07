# mycelius daemons - built as static aarch64 binaries for initramfs
# Part of the Linux kernel tree at linux/drivers/mycelius/

CC ?= aarch64-linux-gnu-gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11 -I.

PROGS := backboardd launchpad init mycc ignitectl discoveryd updatesd

.PHONY: all clean install

all: $(PROGS)

backboardd: backboardd.c lp_proto.h
	$(CC) $(CFLAGS) -static -o $@ backboardd.c

launchpad: launchpad.c render.c lp_proto.h render.h
	$(CC) $(CFLAGS) -static -o $@ launchpad.c render.c

init: init.c
	$(CC) $(CFLAGS) -static -o $@ init.c

mycc: mycc.c
	$(CC) $(CFLAGS) -static -o $@ mycc.c

ignitectl: ignitectl.c
	$(CC) $(CFLAGS) -static -o $@ ignitectl.c

discoveryd: discoveryd.c
	$(CC) $(CFLAGS) -static -o $@ discoveryd.c

updatesd: updatesd.c
	$(CC) $(CFLAGS) -static -o $@ updatesd.c

clean:
	rm -f $(PROGS)

install: $(PROGS)
	install -m 0755 $(PROGS) ../../../rootfs/sbin/
