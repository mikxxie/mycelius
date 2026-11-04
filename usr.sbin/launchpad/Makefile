# launchpad - SpringBoard-style UI shell / render server for mycelius (Linux)
#
# Portable GNU Makefile. Builds on Linux with cc/clang; cross-compiles
# for aarch64-linux-gnu via `make cross`.
# BSD-style compositor: Unix domain socket input → scene graph → framebuffer.
# `make qemu` builds a bootable initramfs image and launches QEMU.

CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11 -I../include
LDLIBS ?=

PROG = launchpad
SRCS = launchpad.c render.c

PREFIX ?= /usr/local
SBINDIR ?= $(PREFIX)/sbin

ROOTFS_DIR = ../../rootfs
TOOLS_DIR = ../../tools
BUILD_DIR = /tmp/mycelius-build
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
# Auto-downloads an aarch64 kernel if missing.
qemu: cross
	@mkdir -p $(BUILD_DIR)
	sh $(TOOLS_DIR)/build-qemu-img.sh $(BUILD_DIR)
	@if [ ! -f $(KERNEL_OUT) ]; then \
		echo ""; \
		echo "ERROR: No kernel found at $(KERNEL_OUT)"; \
		echo "  Please download an aarch64 Linux kernel and place it at:"; \
		echo "    $(KERNEL_OUT)"; \
		echo ""; \
		exit 1; \
	fi
	qemu-system-aarch64 \
		-M virt \
		-cpu cortex-a72 \
		-m 2G \
		-smp 2 \
		-device virtio-gpu-pci \
		-display gtk \
		-kernel $(KERNEL_OUT) \
		-initrd $(INITRAMFS_OUT) \
		-append "console=ttyAMA0 earlyprintk=ttyAMA0 init=/init" \
		-serial stdio \
		-no-reboot

clean:
	rm -f $(PROG)

install: all
	install -m 0755 -d $(DESTDIR)$(SBINDIR)
	install -m 0755 launchpad $(DESTDIR)$(SBINDIR)/launchpad
