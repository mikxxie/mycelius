# mycelius - Linux-based mobile OS daemon stack
# Top-level Makefile: builds kernel + daemons + initramfs, runs QEMU
#
#   make run       - Build everything and launch QEMU with GUI
#   make kernel    - Build Linux kernel (aarch64)
#   make daemons   - Build backboardd + launchpad (static aarch64)
#   make image     - Build initramfs image
#   make update    - Check for mycelius system updates (.msu)

CC ?= cc
CROSS_COMPILE ?= aarch64-linux-gnu-
KERNEL_DIR := linux
DAEMON_DIR := linux/drivers/mycelius
BUILD_DIR := build
IMG := mycelius-arm64.img
INITRAMFS := $(BUILD_DIR)/mycelius-initramfs.gz
KERNEL_IMG := $(BUILD_DIR)/mycelius-kernel
UPDATE_URL := https://mikxxie.k.vu/__private/mycupdates.php

.PHONY: all run kernel daemons image update clean help

all: daemons image kernel

# ---------- Native smoke-test build (x86_64) ----------
daemons:
	$(MAKE) -C $(DAEMON_DIR) CC=cc
	$(MAKE) -C $(DAEMON_DIR) CC=$(CROSS_COMPILE)gcc CFLAGS="-O2 -Wall -std=c11 -I. -static"

# ---------- Kernel build (aarch64, from source) ----------
kernel:
	$(MAKE) -C $(KERNEL_DIR) ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE) defconfig
	$(MAKE) -C $(KERNEL_DIR) ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE) -j$$(nproc) Image
	cp $(KERNEL_DIR)/arch/arm64/boot/Image $(KERNEL_IMG)

# ---------- Initramfs image ----------
image: daemons
	rm -rf $(BUILD_DIR)/rootfs
	mkdir -p $(BUILD_DIR)/rootfs/sbin $(BUILD_DIR)/rootfs/proc $(BUILD_DIR)/rootfs/sys $(BUILD_DIR)/rootfs/dev $(BUILD_DIR)/rootfs/etc $(BUILD_DIR)/rootfs/tmp $(BUILD_DIR)/rootfs/var/log $(BUILD_DIR)/rootfs/var/run
	cp $(DAEMON_DIR)/backboardd $(BUILD_DIR)/rootfs/sbin/backboardd
	cp $(DAEMON_DIR)/launchpad $(BUILD_DIR)/rootfs/sbin/launchpad
	cp $(DAEMON_DIR)/init $(BUILD_DIR)/rootfs/init
	chmod +x $(BUILD_DIR)/rootfs/init $(BUILD_DIR)/rootfs/sbin/backboardd $(BUILD_DIR)/rootfs/sbin/launchpad
	cat > $(BUILD_DIR)/rootfs/etc/fstab << 'FSTAB'
/dev/vda / ext2 defaults 0 1
proc /proc proc defaults 0 0
sysfs /sys sysfs defaults 0 0
devtmpfs /dev devtmpfs defaults 0 0
FSTAB
	cd $(BUILD_DIR)/rootfs && find . -mindepth 1 -printf '%P\0' | cpio --null -o -H newc | gzip > $(INITRAMFS)

# ---------- Run QEMU with GUI ----------
run: image kernel
	qemu-system-aarch64 \
		-M virt \
		-cpu cortex-a72 \
		-m 2G \
		-smp 2 \
		-device virtio-gpu-pci \
		-display gtk \
		-drive file=$(IMG),format=raw,if=none,id=hd0 \
		-device virtio-blk-device,drive=hd0 \
		-kernel $(KERNEL_IMG) \
		-initrd $(INITRAMFS) \
		-append "console=ttyAMA0 earlyprintk=ttyAMA0 root=/dev/vda rw init=/init" \
		-serial stdio \
		-no-reboot

# ---------- Update system (.msu) ----------
update:
	@echo "Checking for mycelius system updates..."
	@curl -s "$(UPDATE_URL)?version=$$(cat VERSION 2>/dev/null || echo 0)" -o /tmp/mycupdate.msu 2>/dev/null || true
	@if [ -f /tmp/mycupdate.msu ] && [ -s /tmp/mycupdate.msu ]; then \
		echo "Update package downloaded: /tmp/mycupdate.msu"; \
		file /tmp/mycupdate.msu; \
	else \
		echo "No update available."; \
	fi

clean:
	rm -rf $(BUILD_DIR)
	$(MAKE) -C $(KERNEL_DIR) ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE) clean 2>/dev/null || true
	$(MAKE) -C $(DAEMON_DIR) clean 2>/dev/null || true

help:
	@echo "mycelius build system"
	@echo "  make run      - Build kernel + daemons + initramfs, launch QEMU GUI"
	@echo "  make kernel   - Build Linux kernel (aarch64)"
	@echo "  make daemons  - Build backboardd + launchpad (static)"
	@echo "  make image    - Build initramfs"
	@echo "  make update   - Check for .msu updates from mikxxie.k.vu"
	@echo "  make clean    - Clean build artifacts"
