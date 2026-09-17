# Mycelis Kernel — bring-up skeleton

This is a freestanding AArch64 QEMU-virt kernel deliberately sized for a 16 MiB
machine. It accepts QEMU's usual direct EL1 handoff or drops from EL2 to EL1, uses no libc,
has a 512-byte physical-page bitmap, fixed-size synchronous-style IPC mailboxes,
and explicit fault/restart bookkeeping for drivers.

Build with `make CROSS=aarch64-none-elf-`; then use `make run CROSS=aarch64-none-elf-`.
If using Clang: `make CC='clang --target=aarch64-none-elf' OBJCOPY=llvm-objcopy CROSS=`.

`drivers.c` is intentionally a non-hardware-touching EL0 driver contract rather
than a misleading in-kernel virtio driver. The necessary production next step is
to build an EL0 context and translation table per driver, mapping only its own
code, IPC page and assigned virtio MMIO window. That preserves the intended
isolation; mapping all RAM in an identity table would not.

The QEMU `virt` machine exposes a device-tree-described virtio-MMIO bus, so a
production discovery service should parse its DTB rather than assume the two
illustrative addresses in `kernel.c`.

## User-space boundary

`userspace/` is separate from the EL1 kernel and contains the public C IPC ABI,
a C++ wrapper, and an example CLI service. Build it with `make userspace
CROSS=aarch64-linux-gnu-`.

The intended BSD-like boundary is a distinct page table per task: map only that
task's code/data/stack, a single IPC page, and (for drivers) its assigned MMIO.
Kernel pages and all other-process RAM must be unmapped at EL0. The current
bring-up kernel defines this ABI and process policy; an EL0 context/page-table
loader is still required before `userspace/cli.elf` can run.

The serial diagnostic prompt supports `help`, `ps`, `mem`, and `restart net` or
`restart blk`. It is temporary EL1 bring-up tooling, not the user-space CLI.

The current RAM VFS supports `pwd`, `ls [DIR]`, `cd DIR`, `cd ..`, `cd /`, and
`mkdir DIR`. It starts empty, is capped at 24 directory nodes with 15-character
names, and is lost on reboot. That is intentional until the isolated virtio-blk
driver and a durable on-disk format are in place.

`net status`, `net up`, and `net down` provide the shell control plane. They do
not send packets yet: the EL0 virtio-net transport/queue implementation is the
required next network milestone. Likewise, ext4 cannot safely be mounted until
the EL0 virtio-blk transport can read and write sectors. When that exists, run
QEMU with a dedicated block image and a virtio-blk device; never test a new
filesystem implementation against an important host file.

`make run-disk CROSS=aarch64-linux-gnu-` creates an 8 MiB raw `mycelius.img`
only when it does not already exist, then attaches it as a virtio-blk device.
The `virtio` shell command reports QEMU virtio-MMIO devices detected on the
transport bus. QEMU's default legacy MMIO transport is detected and its queue
zero is initialized; issuing block requests and processing completions are the
next transport steps.
