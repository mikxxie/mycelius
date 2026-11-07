# mycelius - top-level build
#
#   make qemu      - Build initramfs and launch QEMU with GUI
#   make kernel   - Build Linux kernel (aarch64) from source
#   make daemons  - Cross-compile backboardd + launchpad (static aarch64)
#   make image    - Build initramfs with C init
#   make update   - Check for .msu updates from mikxxie.k.vu

CC ?= cc
CROSS_COMPILE ?= aarch64-linux-gnu-
KERNEL_IMG := linux/arch/arm64/boot/Image
INITRAMFS := build/mycelius-initramfs.gz

.PHONY: all run qemu kernel daemons image update clean help

all: kernel daemons image

# ---------- Build Linux kernel from source (CI does this, local fallback) ----------
kernel:
	$(MAKE) -C linux ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE) defconfig
	$(MAKE) -C linux ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE) -j$$(nproc) Image

# ---------- Cross-compile daemons (static aarch64) ----------
daemons:
	$(MAKE) -C linux/drivers/mycelius clean
	$(MAKE) -C linux/drivers/mycelius CC=$(CROSS_COMPILE)gcc CFLAGS="-O2 -Wall -std=c11 -I. -static"

# ---------- Build initramfs ----------
image: daemons
	rm -rf build/rootfs
	mkdir -p build/rootfs/sbin build/rootfs/proc build/rootfs/sys build/rootfs/dev \
	         build/rootfs/etc build/rootfs/tmp build/rootfs/var/log build/rootfs/var/run \
	         build/rootfs/dev/dri
	$(CROSS_COMPILE)gcc -O2 -static -o build/rootfs/init linux/drivers/mycelius/init.c
	cp linux/drivers/mycelius/backboardd build/rootfs/sbin/backboardd
	cp linux/drivers/mycelius/launchpad build/rootfs/sbin/launchpad
	cp linux/drivers/mycelius/mycc build/rootfs/sbin/mycc
	cp linux/drivers/mycelius/ignitectl build/rootfs/sbin/ignitectl
	cp linux/drivers/mycelius/discoveryd build/rootfs/sbin/discoveryd
	mknod -m 666 build/rootfs/dev/fb0 c 29 0 2>/dev/null || true
	mknod -m 666 build/rootfs/dev/tty c 5 0 2>/dev/null || true
	mknod -m 666 build/rootfs/dev/console c 5 1 2>/dev/null || true
	mknod -m 666 build/rootfs/dev/null c 1 3 2>/dev/null || true
	mknod -m 666 build/rootfs/dev/zero c 1 5 2>/dev/null || true
	chmod +x build/rootfs/init build/rootfs/sbin/backboardd build/rootfs/sbin/launchpad build/rootfs/sbin/mycc build/rootfs/sbin/ignitectl build/rootfs/sbin/discoveryd
	cd build/rootfs && find . -mindepth 1 -printf '%P\0' | cpio --null -o -H newc | gzip > ../mycelius-initramfs.gz

# ---------- Run QEMU with GUI ----------
qemu: image
	@if [ ! -f $(KERNEL_IMG) ]; then \
		echo "Error: kernel not found at $(KERNEL_IMG)"; \
		echo "  Run 'make kernel' first (builds from source)"; \
		echo "  Or CI builds it automatically on push"; \
		exit 1; \
	fi
	qemu-system-aarch64 \
		-M virt \
		-cpu cortex-a72 \
		-m 2G \
		-smp 2 \
		-device virtio-gpu-pci \
		-display gtk \
		-kernel $(KERNEL_IMG) \
		-initrd $(INITRAMFS) \
		-append "console=ttyAMA0 earlyprintk=ttyAMA0 init=/init root=/dev/ram0" \
		-no-reboot

# ---------- Update system (.msu) ----------
update:
	@echo "Checking for mycelius system updates..."
	@CURRENT=$$(cat VERSION 2>/dev/null || echo 0); \
	RESPONSE=$$(curl -s -o /tmp/mycupdate.msu -w "%{http_code}" \
		"https://mikxxie.k.vu/__private/mycupdates.php?version=$$CURRENT" 2>/dev/null); \
	if [ "$$RESPONSE" = "200" ] && [ -s /tmp/mycupdate.msu ]; then \
		echo "Update package downloaded: /tmp/mycupdate.msu"; \
		file /tmp/mycupdate.msu; \
	else \
		echo "No update available (current version: $$CURRENT)"; \
	fi

clean:
	rm -rf build
	$(MAKE) -C linux ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE) clean 2>/dev/null || true
	$(MAKE) -C linux/drivers/mycelius clean 2>/dev/null || true

help:
	@echo "mycelius build system"
	@echo "  make qemu      - Build initramfs and launch QEMU with GUI"
	@echo "  make kernel   - Build Linux kernel (aarch64) from source"
	@echo "  make daemons  - Cross-compile backboardd + launchpad (static)"
	@echo "  make image    - Build initramfs"
	@echo "  make update   - Check for .msu updates from mikxxie.k.vu"
	@echo "  make clean    - Clean build artifacts"
