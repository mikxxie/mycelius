# EL0 Driver Isolation - Documentation

## Overview

EL0 is the **least privileged execution level** in AArch64 (ARM64). In Mycelius, EL0 is intended to host **isolated driver processes**. Each driver runs at EL0 with its own translation tables, mapped only to:
- Its own code page
- Its own IPC page
* Its assigned MMIO window (e.g., 0x0a000000 for virtio-net)

This is the core isolation mechanism: a buggy or malicious driver cannot access kernel memory or other drivers' memory.

---

## How It Works (Architecture)

### Translation Tables (mmu.c)

Each EL0 task has its own 4-level translation table:

```
L0 (pgd) -> L1 -> L2 -> L3 (actual pages)
```

For a 4KB page at VA `0x400000`:
- `L0[0]` -> L1 page table
- `L1[0]` -> L2 page table  
- `L2[1]` (index for 0x400000) -> L3 page table
- `L3[0]` = physical page frame + attributes

### Queue Table (mmu_task_init)

`mmu_task_init(u32 id)` builds per-task page tables:
- Maps driver code at `0x400000` (TASK_CODE_VA)
- Maps IPC page at `0x4001000` (TASK_IPC_VA)
- Maps MMIO window at `0x0a000000` (device MMIO)

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
   - `TTBR0_EL1` = task's page table
   - `SP_EL0` = task's stack pointer
   - `ELR_EL1` = task's PC (entry point)
   - `SPSR_EL1` = 0x00000000 (EL0t, SP_EL0, no mask)
3. `eret` switches to EL0 at `pc`
4. Driver runs until SVC #4 (exit) or exception
5. `user_exception` handles it, switches back to kernel TTBR0

### SPSR_EL1 Values

| Value | Meaning |
|-------|---------|
| `0x00000000` | EL0t, SP_EL0, DAIF all masked |
| `0x3c0` | EL1h, SP_EL1, DAIF masked |
| `0x3c5` | EL1h, DAIF masked |

---

## What Works

- Kernel boots to serial shell
- `net ping 10.0.2.2` works (virtio-net stack functional)
- ext4 mounts read-write
- `make run`, `make run-disk`, `make run-gui` work
- Dev headers in `dev/` are complete
- Page table infrastructure exists in `mmu.c`

## What Is Broken (EL0 Bring-Up)

`task_launch(1, ...)` calls `eret` to enter EL0, but the system hangs immediately after `task_switch done, about to eret`.

**Likely causes:**
1. `virtio_net_driver_entry` uses `uart_puts()` at EL0 — UART is at kernel address `0x09000000`, NOT mapped in task page table → **data abort**
2. `SPSR_EL1 = 0x00000000` may be incorrect for EL0 execution
3. The driver code is copied to `task_code_pa` but mapped at `TASK_CODE_VA` — need to verify mapping matches
4. `net_rx_seen` / `net_tx_seen` static variables in `virtio.c` are kernel-space; accessing them from EL0 faults

**To fix:**
1. Remove `uart_puts` from driver code (drivers must not call kernel functions directly)
2. Ensure `SPSR_EL1 = 0x3c0` (EL1h, SP_EL0) for proper EL0 return
3. Verify `QueuePFN` register is written with correct physical page number
4. Add debug output after `eret` to confirm EL0 entry

## Next Steps

1. Fix `virtio_net_driver_entry` — remove `uart_puts`, use IPC page only
2. Test `task_launch(1, ...)` — should reach SVC #4 and return to `kernel_after_user`
3. Then implement `net_poll()` processing at EL0 (driver receives packets via IPC)
4. Then transition blk driver to EL0
5. Then GUI (virtio-gpu framebuffer)

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
