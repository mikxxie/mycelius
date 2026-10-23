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
- Maps driver code at `0x400000` (TASK_CODE_VA)
- Maps IPC page at `0x00404000` (`TASK_IPC_VA`)
- Maps the assigned MMIO page at `0x00403000` (`NET_MMIO_VA`)

Direct use of these distinct roots still faults at `TASK_CODE_VA`. The verified
bring-up path instead uses `mmu_task_activate()` immediately before each
sequential probe to replace the established root's four EL0 PTEs (code, stack,
IPC and MMIO). Kernel RAM, UART and every other device mapping remain EL1-only.
This isolates the active driver but is not yet a simultaneously runnable
per-task address-space design.

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
};
```

### EL0 Entry Flow

1. `task_launch(id, x0, x1, x2)` calls `task_switch(id)` then `eret`
2. `task_switch` loads:
   - `TTBR0_EL1` = established root with the active task's four-page EL0 window
   - `SP_EL0` = task's stack pointer
   - `ELR_EL1` = task's PC (entry point)
   - `SPSR_EL1` = 0x00000000 (EL0t, SP_EL0, no mask)
3. `eret` switches to EL0 at `pc`
4. Driver runs until SVC #4 (exit) or exception
5. `user_exception` handles it, switches back to kernel TTBR0

### SPSR_EL1 Values

| Value | Meaning |
|-------|---------|
| `0x00000000` | EL0t, SP_EL0, interrupts unmasked |
| `0x3c0` | EL0t, SP_EL0, DAIF masked |
| `0x3c5` | EL1h, SP_EL1, DAIF masked |

---

## What Works

- Kernel boots to serial shell
- `net ping 10.0.2.2` works (the kernel-owned bring-up net stack is functional)
- ext4 mounts read-write
- `make run`, `make run-disk`, `make run-gui` work
- Dev headers in `dev/` are complete
- Page table infrastructure exists in `mmu.c`

## Current EL0 Bring-Up Status

Net and block EL0 probes are verified in QEMU. Boot initializes the
kernel-owned transports and mounts ext4 first, then runs the net probe at EL0,
its SVC #4 return path, the block probe, and finally the shell. This order
prevents a probe from selecting the non-persistent RAM VFS.

Production queue state and network/block transport remain kernel-owned; direct
loading of each distinct task root is still an unresolved next step.

## Next Steps

1. Resolve the distinct task-root `TASK_CODE_VA` translation fault and run the
   probes with their own `TTBR0_EL1` roots.
2. Move queue ownership and bounded request processing into the EL0 drivers.
4. Replace diagnostic SVC output with validated IPC messages and add fault
   reporting/restart for non-SVC synchronous exceptions.
5. Then GUI (virtio-gpu framebuffer).

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
