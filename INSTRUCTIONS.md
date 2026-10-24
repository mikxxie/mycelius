# Mycelius

Mycelius is a from-scratch phone operating system: a freestanding AArch64
kernel with its own drivers, filesystem, shell, and network stack. It is
developed first on QEMU `virt` under a hard 16 MiB RAM budget before moving
to real phone hardware.

## What it is

- **Freestanding AArch64 kernel** — no libc, no allocation, runs at EL1 on
  QEMU `virt` (16 MiB RAM).
- **Own drivers** — virtio-blk (storage) and virtio-net (networking) brought
  up in-kernel, intended to become isolated EL0 processes.
- **Own filesystem** — ext4 on virtio-blk, read-write and persistent across
  reboots, with a RAM VFS fallback when no disk is attached.
- **Own network stack** — Ethernet framing, ARP, IPv4, and ICMP echo (ping)
  over the legacy virtio-net transport.
- **Shell + editor** — a serial shell (`mycelius>`) and the MNU text editor.

## Build & run

```sh
make                 # build mycelius.bin
make run             # serial console, virtio-net attached (user networking)
make run-disk        # also attach mycelius.img as virtio-blk and mount ext4
make run-gui         # open a QEMU graphical window (for future GUI work)
make format-disk     # DESTRUCTIVE: reformat mycelius.img
```

`make run-gui` uses `-display gtk` by default; override with
`make run-gui QEMU_DISPLAY=curses` (or `none`) on headless hosts.

The cross toolchain prefix defaults to `aarch64-none-elf-`; override with
`make CROSS=...`.

## Repo layout

- `boot.S`, `vectors.S`, `el0.S` — entry, exception vectors, EL0 bring-up.
- `kernel.c` / `kernel.h` — kernel main, page allocator, task model, UART.
- `virtio.c` — virtio-MMIO discovery and the virtio-blk / virtio-net
  transports (legacy queues).
- `net.c` — Ethernet / ARP / IPv4 / ICMP stack.
- `ext4.c`, `vfs.c` — ext4 driver and the VFS layer.
- `cli.c` — the serial shell and MNU editor.
- `mmu.c`, `ipc.c`, `drivers.c` — MMU, IPC mailboxes, driver stubs.
- `userspace/` — the EL0 IPC ABI and an example CLI service (separate build).
- `dev/` — development headers / public syscall ABI (ipc.h, net.h, drv.h,
  fs.h, syscall.h plus C++ wrappers *.hpp and umbrella dev/mycelius.h).
- `linker.ld` — kernel link script (loads at 0x40080000).

## Network

The stack speaks Ethernet, ARP, IPv4, and ICMP echo. The guest uses
`10.0.2.15/24` (QEMU user networking) with the virtio-net MAC read from the
device config space. Shell commands:

- `net status` — show MAC / link state.
- `net up` / `net down` — control the interface.
- `net ping <ip>` — ARP resolve then ICMP echo (e.g. `net ping 10.0.2.2`).

Host→guest ping depends on slirp ARP behaviour; guest→host ping is the
reliable end-to-end check.

## Docs

- **AGENTS.md** — agent handoff: current working state and the required next
  steps. Start here if you are picking up development.
- **README.md** — original bring-up notes and design constraints.

## Roadmap

1. virtio-net stack (Ethernet / ARP / IPv4 / ICMP) — **done**.
2. Per-task EL0 translation tables; move drivers into isolated EL0 processes — **done** (net and blk drivers run at EL0 via SVC).
3. Development headers / syscall surface — **done** (see dev/).
4. GUI (virtio-gpu framebuffer + input) — `make run-gui` is the entry point.
5. Phone hardware bring-up.

## GUI Development (in progress)

The GUI stack will consist of:

- **virtio-gpu driver** (`drivers/virtio-gpu.c`) — MMIO discovery, queue setup, framebuffer allocation, display configuration.
- **Framebuffer driver** (`drivers/framebuffer.c`) — abstracts the GPU framebuffer as a linear framebuffer.
- **GUI framework** (`gui.c`) — basic drawing primitives (pixel, line, rect, text), window management, input handling.

The `make run-gui` target now also attaches `mycelius.img` as virtio-blk so the GUI can access the filesystem.

GUI draw commands are sent via IPC from user-space drivers to the kernel framebuffer driver.
