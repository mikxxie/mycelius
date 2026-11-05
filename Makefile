# mycelius - top-level build
#
#   make run      - Build everything and launch QEMU with GUI
#   make kernel   - Build Linux kernel (aarch64) from source
#   make daemons  - Cross-compile backboardd + launchpad (static aarch64)
#   make image    - Build initramfs with C init
#   make update   - Check for .msu updates from mikxxie.k.vu

CC ?= cc
CROSS_COMPILE ?= aarch64-linux-gnu-
KERNEL_IMG := linux/arch/arm64/boot/Image
INITRAMFS := build/mycelius-initramfs.gz

.PHONY: all run kernel daemons image update clean help

all: kernel daemons image

# ---------- Build Linux kernel from source (CI does this, local fallback) ----------
kernel:
	$(MAKE) -C linux ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE) defconfig
	$(MAKE) -C linux ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE) -j$$(nproc) Image

# ---------- Cross-compile daemons (static aarch64) ----------
daemons:
	$(MAKE) -C linux/drivers/mycelius CC=$(CROSS_COMPILE)gcc CFLAGS="-O2 -Wall -std=c11 -I. -static"

# ---------- Build initramfs ----------
image: daemons
	rm -rf build/rootfs
	mkdir -p build/rootfs/sbin build/rootfs/proc build/rootfs/sys build/rootfs/dev \
	         build/rootfs/etc build/rootfs/tmp build/rootfs/var/log build/rootfs/var/run
	cp linux/drivers/mycelius/backboardd build/rootfs/sbin/backboardd
	cp linux/drivers/mycelius/launchpad build/rootfs/sbin/launchpad
	cp linux/drivers/mycelius/init.c build/rootfs/init
	chmod +x build/rootfs/init build/rootfs/sbin/backboardd build/rootfs/sbin/launchpad
	printf '/dev/vda / ext2 defaults 0 1\nproc /proc proc defaults 0 0\nsysfs /sys sysfs defaults 0 0\ndevtmpfs /dev devtmpfs defaults 0 0\n' > build/rootfs/etc/fstab
	cd build/rootfs && find . -mindepth 1 -printf '%P\0' | cpio --null -o -H newc | gzip > ../$(INITRAMFS)

# ---------- Run QEMU with GUI ----------
run: image kernel
	qemu-system-aarch64 \
		-M virt \
		-cpu cortex-a72 \
		-m 2G \
		-smp 2 \
		-device virtio-gpu-pci \
		-display gtk \
		-drive file=mycelius-arm64.img,format=raw,if=none,id=hd0 \
		-device virtio-blk-device,drive=hd0 \
		-kernel $(KERNEL_IMG) \
		-initrd $(INITRAMFS) \
		-append "console=ttyAMA0 earlyprintk=ttyAMA0 root=/dev/vda rw init=/init" \
		-serial stdio \
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
	@echo "  make run      - Build everything and launch QEMU with GUI"
	@echo "  make kernel   - Build Linux kernel (aarch64) from source"
	@echo "  make daemons  - Cross-compile backboardd + launchpad (static)"
	@echo "  make image    - Build initramfs"
	@echo "  make update   - Check for .msu updates from mikxxie.k.vu"
	@echo "  make clean    - Clean build artifacts"
