# backboardd - low-level input daemon for mycelius (Linux-based mobile OS)
#
# Portable GNU Makefile. Builds on Linux with cc/clang; cross-compiles
# for aarch64-linux-gnu via `make cross`.
# BSD-style daemon: input evdev → Unix domain socket → LaunchPad.
# `make qemu` builds a bootable initramfs image and launches QEMU.

CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11 -I../include
LDLIBS ?=

PROG = backboardd
SRCS = backboardd.c

PREFIX ?= /usr/local
SBINDIR ?= $(PREFIX)/sbin

ROOTFS_DIR = ../../rootfs
TOOLS_DIR = ../../tools
IMG_OUT = /tmp/mycelius-arm64.img
INITRAMFS_OUT = /tmp/mycelius-initramfs.gz
KERNEL_OUT = /tmp/mycelius-kernel

.PHONY: all clean install cross qemu run-qemu

all: $(PROG)

$(PROG): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Cross-build for aarch64-linux-gnu (static, for initramfs).
#   make cross CC=aarch64-linux-gnu-gcc
cross: CC = aarch64-linux-gnu-gcc
cross: CFLAGS += -static
cross: $(SRCS)
	$(CC) $(CFLAGS) -o $(PROG) $^ $(LDLIBS)
	file $(PROG) | grep -q aarch64

# Build QEMU boot image (initramfs + kernel) and launch QEMU.
qemu: cross
	sh $(TOOLS_DIR)/build-qemu-img.sh $(IMG_OUT)
	@if [ ! -f $(KERNEL_OUT) ]; then \
		echo "ERROR: No kernel available. Download an aarch64 kernel to $(KERNEL_OUT)"; \
		exit 1; \
	fi
	qemu-system-aarch64 \
		-M virt \
		-cpu cortex-a72 \
		-m 2G \
		-smp 2 \
		-kernel $(KERNEL_OUT) \
		-initrd $(INITRAMFS_OUT) \
		-append "console=ttyAMA0 earlyprintk=ttyAMA0 root=/dev/vda2 rw init=/init" \
		-display none \
		-serial stdio \
		-no-reboot

clean:
	rm -f $(PROG)

install: all
	install -m 0755 -d $(DESTDIR)$(SBINDIR)
	install -m 0755 backboardd $(DESTDIR)$(SBINDIR)/backboardd
