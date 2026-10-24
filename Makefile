CROSS ?= aarch64-none-elf-
# Use the selected cross prefix even when the host exports CC=cc.  Command-line
# overrides remain supported (for example CC='clang --target=aarch64-none-elf').
CC    := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy

# No libc, allocation, or exception unwinding: this keeps the image predictable.
CFLAGS := -Wall -Wextra -Werror -O2 -ffreestanding -fno-builtin -fno-stack-protector \
          -nostdlib -nostartfiles -mgeneral-regs-only -march=armv8-a
LDFLAGS := -T linker.ld -nostdlib -Wl,--build-id=none
OBJS := boot.o vectors.o el0.o kernel.o ipc.o drivers.o cli.o mmu.o vfs.o net.o virtio.o ext4.o gui.o

all: mycelius.bin

userspace:
	$(MAKE) -C userspace CROSS=$(CROSS)

mycelius.elf: $(OBJS) linker.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)

mycelius.bin: mycelius.elf
	$(OBJCOPY) -O binary $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

QEMU ?= qemu-system-aarch64
QEMU_DISPLAY ?= gtk
QEMU_COMMON := -M virt -cpu cortex-a72 -m 16M -kernel mycelius.bin -netdev user,id=net0 -device virtio-net-device,netdev=net0

run: mycelius.bin disk
	$(QEMU) $(QEMU_COMMON) -nographic -drive if=none,file=mycelius.img,format=raw,id=blk0 -device virtio-blk-device,drive=blk0

run-gui: mycelius.bin disk
	$(QEMU) $(QEMU_COMMON) -display $(QEMU_DISPLAY) -serial mon:stdio -drive if=none,file=mycelius.img,format=raw,id=blk0 -device virtio-blk-device,drive=blk0 -device virtio-gpu-device

disk:
	@test -e mycelius.img || qemu-img create -f raw mycelius.img 8M

run-disk: mycelius.bin disk
	$(QEMU) $(QEMU_COMMON) -nographic -drive if=none,file=mycelius.img,format=raw,id=blk0 -device virtio-blk-device,drive=blk0

format-disk: disk
	mkfs.ext4 -F mycelius.img

size: mycelius.elf
	$(CROSS)size $<

clean:
	rm -f $(OBJS) mycelius.elf mycelius.bin
	$(MAKE) -C userspace clean

.PHONY: all run run-gui run-disk disk format-disk size clean userspace
