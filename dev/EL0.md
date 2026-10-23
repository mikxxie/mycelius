# EL0 Driver Isolation - Documentation

## Overview

EL0 is the **least privileged execution level** in AArch64 (ARM64). In Mycelius, EL0 hosts isolated driver probes. The active probe's EL0 window maps only:
- Its own code page
- Its own IPC page
- Its assigned MMIO page, mapped at `NET_MMIO_VA` (`0x00403000`)

This is the core isolation mechanism: a buggy or malicious driver cannot access kernel memory or other drivers' memory.

---

## How It Works (Architecture)

### Translation Tables (mmu.c)

The experimental per-task roots use a 4-level translation table:

```
L0 (pgd) -> L1 -> L2 -> L3 (actual pages)
```

For a 4KB page at VA `0x400000`:
- `L0[0]` -> L1 page table
- `L1[0]` -> L2 page table  
- `L2[2]` (index for 0x400000) -> L3 page table
- `L3[0]` = physical page frame + attributes

### Task Table (mmu_task_init)

`mmu_task_init(u32 id)` builds per-task page tables:
- Maps driver code at `0x400000` (`TASK_CODE_VA`)
- Maps driver stack at `0x402000` (`TASK_STACK_VA`)
- Maps IPC page at `0x00404000` (`TASK_IPC_VA`)
- Maps assigned MMIO page at `0x00403000` (`NET_MMIO_VA`)
- Maps driver-owned virtio queues at `0x00500000` (`TASK_QUEUE0_VA`, `TASK_QUEUE1_VA`), data page at `0x00504000`, and request page at `0x00505000`

Each task has its own root `task_pgd[id]` which is directly installed into `TTBR0_EL1` during `task_switch()`. Kernel RAM is mapped read-only (EL1-only) at L1[1] for exception handling without granting EL0 access to kernel memory.

### Task Context (struct task in kernel.h)

```c
struct task {
    u32 id;
    enum task_state state;
    u64 pgd;              // Physical address of EL0 page table
    u64 sp_el0;           // Stack pointer for EL0
    u64 pc;               // Program counter (entry point)
    u64 spsr;             // Saved Program Status Register
    u64 mmio_base;        // Driver's MMIO base
    u64 mmio_size;        // MMIO size
    u64 queue0_pa;        // Physical address of queue0 ring
    u64 queue1_pa;        // Physical address of queue1 ring
    u64 data_pa;          // Physical address of data buffer
    u64 req_pa;           // Physical address of request buffer
};
```

### EL0 Entry Flow

1. `task_launch(id, x0, x1, x2)` calls `task_switch(id)` then `eret`
2. `task_switch` loads:
   - `TTBR0_EL1` = `tasks[id].pgd` (distinct per-task root table)
   - `SP_EL0` = task's stack pointer
   - `ELR_EL1` = task's PC (entry point)
   - `SPSR_EL1` = 0x00000000 (EL0t, SP_EL0, no mask)
3. `eret` switches to EL0 at `pc`
4. Driver runs its queue setup, I/O processing, and communicates with kernel via IPC
5. `user_exception` handles syscalls (IPC send/recv reinstall task TTBR0_EL1 on return to EL0; exit switches back to kernel TTBR0 and returns to EL1)

### SPSR_EL1 Values

| Value | Meaning |
|-------|---------|
| `0x00000000` | EL0t, SP_EL0, interrupts unmasked |
| `0x3c0` | EL0t, SP_EL0, DAIF masked |
| `0x3c5` | EL1h, SP_EL1, DAIF masked |

---

## What Works

- Kernel boots to serial shell
- Per-task `TTBR0_EL1` roots run both net and block driver probes at EL0 with distinct page tables
- Driver-owned virtio queue rings, request headers, and data buffers mapped into EL0 address spaces
- Virtio-blk driver at EL0 performs sector 0 read using physical addresses in DMA descriptors
- Drivers communicate completion via validated IPC (`ipc_send`) and handle service loop requests
- Synchronous non-SVC exceptions (instruction abort, data abort, alignment faults) log structured fault diagnostics (PC, FAR, ESR)
- Kernel supervisor (`driver_supervise()`) detects faulted driver tasks and automatically resets their execution context back to `TASK_READY`
- Shell commands `crash net` and `crash blk` test fault injection and supervisor recovery
- `net ping 10.0.2.2` works
- ext4 mounts read-write and passes host `e2fsck -fn` cleanly
- `make run`, `make run-disk`, `make run-gui` work
- Dev headers in `dev/` are complete

## Current EL0 Bring-Up Status

Net and block EL0 drivers are verified in QEMU running under their own distinct `TTBR0_EL1` roots. The drivers own their queue descriptors and buffers and report status back to the kernel via validated IPC. Synchronous exception fault handling and automatic supervisor restart have been verified.

## Next Steps

1. GUI (virtio-gpu framebuffer + display/input for `make run-gui`).

---

## Files Involved

| File | Role |
|------|------|
| `kernel.c` | `kernel_main()` launches drivers at EL0 via `task_launch()` |
| `kernel.h` | Declares `task_launch`, `task_switch`, `mmu_task_init`, `mmu_map_page` |
| `mmu.c` | `mmu_task_init()` builds page tables; `mmu_map_page()` maps pages; `user_exception()` handles SVC |
| `virtio.c` | `virtio_net_driver_entry()`, `virtio_blk_driver_entry()` — EL0 driver entry points |
| `drivers.c` | Same as virtio.c (duplicate) |
| `vectors.S` | EL1 exception vector — catches SVC from EL0 |
| `linker.ld` | Kernel link script (loads at 0x40080000) |

---

## Dev Headers (dev/)

| File | Purpose |
|------|---------|
| `dev/ipc.h` | IPC message types, `ipc_send`, `ipc_recv`, `ipc_exit` |
| `dev/net.h` | Ethernet/ARP/IPv4/ICMP API |
| `dev/drv.h` | Driver registration, MMIO read/write |
| `dev/fs.h` | File open/read/write/cat/mkdir/rm |
| `dev/syscall.h` | Syscall numbers (exit=4, write=3, ipc_send=5, ipc_recv=6) |
| `dev/mycelius.h` | Umbrella header |
