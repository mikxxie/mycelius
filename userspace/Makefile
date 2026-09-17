CROSS ?= aarch64-none-elf-
CC := $(CROSS)gcc
CFLAGS := -Wall -Wextra -Werror -O2 -ffreestanding -fno-builtin -fno-stack-protector -nostdlib -nostartfiles -Iinclude -march=armv8-a
all: cli.elf
cli.elf: cli.c syscall.S linker.ld
	$(CC) $(CFLAGS) -T linker.ld -Wl,--build-id=none -o $@ cli.c syscall.S
clean:
	rm -f cli.elf
.PHONY: all clean
