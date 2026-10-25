# Task: Fix Mycelius's VirtIO GPU display initialization properly

You are working on my AArch64 hobby OS, **Mycelius**, in the current repository:

- Mycelius repository: `~/mycelius`
- BSD kernel source/reference tree: `../mycelius-bsd/src`
- Main GPU implementation: `virtio.c`
- Build/run targets include `make run-gui`, `make run-gui-1`, `make run-gui-2`, and `make run-gui-3`.

## 1. The actual problem

Mycelius boots successfully under QEMU, mounts its ext4 filesystem read-write, and provides a serial shell. Running `gui` draws a software framebuffer and calls `virtio_gpu_display()`, but the QEMU graphical window remains black and reports:

`Guest has not initialized the display (yet).`

The software framebuffer is reported as:

- Address: `0x400cb000`
- Resolution: `320x240`
- Pixel depth: 32 bits

The serial output claims:

- `gui: framebuffer demo drawn`
- `gui: calling virtio_gpu_display()...`
- `virtio-gpu: TRANSFER_TO_HOST_2D done`
- `virtio-gpu: framebuffer displayed on scanout 0`

Those success messages are NOT proof that the commands were completed successfully. The current implementation frequently times out without reporting the timeout and does not validate most GPU command responses.

## 2. QEMU configurations already tested

The following variations have already been attempted:

| Target | Relevant configuration | Result |
|---|---|---|
| `make run-gui` | 64 MiB, `virtio-gpu-device` | Black display |
| `make run-gui-1` | 64 MiB, `virtio-gpu-device`, `-vga none` | Black display |
| `make run-gui-2` | 64 MiB, `virtio-gpu-pci` | Driver reports GPU device not found |
| `make run-gui-3` | 128 MiB, `virtio-gpu-device` | Black display |

All variants use the QEMU ARM `virt` machine and GTK display. Stop randomly changing memory sizes and GPU device types. First fix and validate the guest implementation.

Important distinction: the current driver discovers VirtIO devices by scanning VirtIO-MMIO slots starting at `0x0a000000`, with `0x200` spacing. A PCI GPU is not discoverable through that MMIO scan. Do not switch to `virtio-gpu-pci` unless you also implement or reuse proper PCI enumeration and transport support.

## 3. CRITICAL: inspect the BSD reference source

I specifically pointed the coding agent at `../mycelius-bsd/src`, which contains a copy of FreeBSD source. It has already been inspected superficially, but the implementation still does not work.

This time, actually investigate the relevant source code.

1. Inspect the repository layout and identify Mycelius's architecture, MMU configuration, memory map, device discovery, queue implementation, framebuffer allocator, and build system.
2. Search the BSD source tree for VirtIO transport, VirtIO-MMIO, virtqueues, DMA/busdma handling, framebuffer drivers, display initialization, and any usable VirtIO GPU implementation.
3. Trace the FreeBSD code from device discovery through feature negotiation, queue allocation, command submission, completion, and error handling.
4. Identify which parts of FreeBSD are directly reusable as design references and which depend on FreeBSD's busdma, bus-space, PCI, VM, locking, or driver frameworks.
5. Do not assume FreeBSD has an in-tree VirtIO GPU driver just because it has VirtIO support. Verify what actually exists in this checkout. If necessary, inspect the linked external implementation below as an additional reference.
6. Compare the BSD implementation against Mycelius's actual code and explain the concrete discrepancies before implementing fixes.

Do not simply copy a large BSD driver into Mycelius. Port the relevant algorithms and protocol handling to Mycelius's own interfaces and memory-management model.

## 4. Audit `virtio.c` thoroughly

The current `virtio_gpu_display()` implementation has several suspicious or incorrect areas. Verify every item against the VirtIO specification before changing it.

### A. VirtIO-MMIO notification register

The current code contains:

`gpu_notify_off = R(base, 0x088);`

It then uses this value as the MMIO notification register offset.

For VirtIO-MMIO, `QueueNotify` is at offset `0x050`. Offset `0x088` is not a VirtIO-MMIO queue-notification-offset configuration register. That type of notification-offset mechanism belongs to other transports.

Use the correct transport-specific notification mechanism. For the current MMIO implementation, notify queue 0 by writing queue index 0 to `base + 0x050`, assuming the relevant notification features were not negotiated.

Also audit the rest of the transport initialization: do not blindly apply legacy MMIO registers to modern VirtIO-MMIO devices.

### B. Available-ring index

Each GPU command currently resets the available-ring entry and index to fixed values:

- `avail[2] = 0`
- `avail[3] = 1`
- `avail[1] = 1`

This is repeated for GET_DISPLAY_INFO, RESOURCE_CREATE_2D, RESOURCE_ATTACH_BACKING, RESOURCE_SET_SCANOUT, TRANSFER_TO_HOST_2D, and RESOURCE_FLUSH.

Implement correct virtqueue producer-index management. Publish each descriptor chain in the appropriate available-ring slot, advance the index correctly, and preserve ring state across commands.

Do not reset the queue indices for each command. Ensure the selected ring size matches the actual negotiated queue size and that the descriptor chain is valid.

### C. Completion handling

The current implementation calculates completion targets after notification in several places and allows timeout loops to fall through while printing success.

Correct the ordering:

1. Prepare the command and descriptors.
2. Record the expected completion based on the used-ring index.
3. Publish the available-ring entry and updated producer index with appropriate memory barriers.
4. Notify the device.
5. Wait for completion using a bounded timeout.
6. Validate the used-ring element and response.
7. Return a real error if completion does not arrive or the device reports failure.

Do not assume the next used-ring entry is always the one you expect without checking its descriptor-chain head and ring state.

Use the right memory-ordering primitives for the AArch64 memory attributes and DMA/coherency model. A generic `dmb ish` must not automatically be assumed sufficient for every DMA or device-memory interaction.

### D. GPU command structures and response validation

Audit every GPU command against the actual VirtIO GPU specification.

In particular:

- GET_DISPLAY_INFO needs sufficient response-buffer space for the complete display-information response.
- RESOURCE_CREATE_2D must use a valid resource ID, supported pixel format, and dimensions.
- RESOURCE_ATTACH_BACKING must have the exact specified structure layout and correctly encoded memory entries.
- RESOURCE_SET_SCANOUT must use a valid scanout and resource.
- TRANSFER_TO_HOST_2D must use the correct resource, rectangle, offset, and backing-memory layout.
- RESOURCE_FLUSH must be issued only after successful setup and transfer.

**Check structure sizes and field offsets.** In the current code, `struct gpu_resource_attach_backing` includes an extra `padding` member before the memory entries. Verify that this matches the specification; do not preserve the extra field if it shifts `entries` to the wrong offset.

The code currently checks the response type for GET_DISPLAY_INFO but largely ignores responses for subsequent commands. Validate each response against the appropriate success or error response type. Do not print success after a timeout or failed command.

Also verify:

- The selected GPU queue is actually supported.
- Feature negotiation reads the device's offered features and only negotiates supported features.
- `FEATURES_OK` is checked after negotiation.
- Queue descriptor, available-ring, and used-ring addresses and alignments are correct.
- `QueueReady` is set at the correct point in initialization.
- Resource creation and backing attachment succeed before scanout configuration.
- Fence flags and fence IDs are used consistently if fencing is enabled.
- The command and response buffers remain valid for the lifetime of the transaction.

### E. Framebuffer physical address and pixel format

The code obtains the framebuffer address through `gui_fb_addr()` and passes it to RESOURCE_ATTACH_BACKING.

Verify whether that address is a guest physical address suitable for VirtIO DMA, or merely a kernel virtual address. Inspect Mycelius's page tables, address translation, and memory mapping. Do not assume virtual and physical addresses are interchangeable unless the current mapping and device model prove it.

Verify that the framebuffer backing covers the complete image, that `width * height * 4` cannot overflow, and that the pixel format and channel order match the software renderer.

The current implementation uses `GPU_FORMAT_RGBA8888`; compare that choice against the bytes actually written by the GUI demo and the supported QEMU format.

### F. Device discovery and initialization ownership

`virtio_probe()` performs generic device and queue setup, while `virtio_gpu_display()` resets and reconfigures the GPU.

Audit whether this double initialization leaves stale queue state or creates conflicting assumptions. Give one component clear ownership of GPU initialization and queue lifetime.

The MMIO discovery code must distinguish device IDs and versions correctly. Do not break the already working block and network paths while repairing GPU support.

The PCI experiment returning `virtio-gpu: device not found` is consistent with an MMIO-only discovery path. Do not treat that result as evidence that the GPU protocol itself is broken.

## 5. Investigate the separate block-driver fault

Boot logs also show:

`mycelius: task 2 fault: data-abort PC=0x0040002c FAR=0x00504000 ESR=0x92000047`

This appears during the block-driver startup path:

`mycelius: calling virtio_blk_driver_entry at EL0`

followed by:

`mycelius: restarted driver`

The OS subsequently mounts ext4 read-write, so this fault does not necessarily explain the black GPU window. Investigate it separately.

Trace the faulting instruction at `0x0040002c`, the fault address `0x00504000`, the EL0/EL1 access policy, and the driver's mapped memory. Check whether the restart is expected, masks a real bug, or affects shared VirtIO initialization. Do not conflate this failure with GPU display initialization without evidence.

## 6. Reference links

Use these sources as technical references and verify your implementation against them:

1. **Official VirtIO 1.3 specification** — device initialization, modern MMIO transport, virtqueues, feature negotiation, memory ordering, and queue notifications:

   https://docs.oasis-open.org/virtio/virtio/v1.3/virtio-v1.3.html

2. **Official VirtIO GPU specification section** — GPU control commands, response structures, resources, backing memory, scanout, transfer, and flush:

   https://docs.oasis-open.org/virtio/virtio/v1.3/virtio-v1.3.html#x1-41100011

   If that section anchor has changed, use the GPU device chapter in the same specification.

3. **QEMU VirtIO GPU documentation** — device variants and display backends:

   https://www.qemu.org/docs/master/system/devices/virtio/virtio-gpu.html

4. **QEMU ARM `virt` machine documentation** — available devices and platform configuration:

   https://www.qemu.org/docs/master/system/arm/virt

5. **FreeBSD kernel source tree** — browse the actual source, especially `sys/dev/virtio/` and `sys/arm64/`:

   https://github.com/freebsd/freebsd-src/tree/main/sys/dev/virtio

   Local reference tree to inspect first: `../mycelius-bsd/src`

6. **FreeBSD VirtIO documentation** — useful background on its transport and supported devices:

   https://man.freebsd.org/cgi/man.cgi?query=virtio&sektion=4

7. **Experimental FreeBSD DRM/KMS project mentioning VirtIO GPU support** — inspect only if needed for a GPU implementation reference; it is not proof that the local FreeBSD source tree contains a working driver:

   https://github.com/ravynsoft/drm-kmod

Prefer official specifications and actual implementation code over guesses or unverified comments.

## 7. Required implementation workflow

Do not stop after producing an analysis or a patch proposal. Work on the actual repository.

1. Inspect the current Git state and preserve unrelated user changes.
2. Read all relevant files and trace the call path from `gui` to `virtio_gpu_display()`.
3. Compare the implementation against the specification and BSD source.
4. Identify the root causes with specific file names, functions, and code locations.
5. Edit the actual source files to fix the issues.
6. Add meaningful error reporting for feature negotiation, queue setup, command submission, timeouts, and response errors.
7. Build the kernel using the repository's real build system.
8. Run `make run-gui` first using the existing default configuration.
9. Check whether the GTK display actually shows the framebuffer. Successful UART log messages alone are not sufficient.
10. Run any available relevant tests and check that existing networking, block I/O, and ext4 functionality has not regressed.
11. If the graphical display still fails, continue debugging using the new evidence. Do not claim success if QEMU has not visibly displayed the framebuffer.

Do not randomly increase RAM, switch between GPU variants, or rewrite unrelated drivers as a substitute for debugging.

Do not remove working functionality merely to make the GPU code compile. Do not introduce speculative abstractions or a wholesale redesign unless the existing architecture genuinely requires it.

## 8. Final report

When finished, report:

- The actual root cause(s) found.
- Which files were changed and why.
- Which FreeBSD source files were consulted and what relevant behavior was learned.
- The protocol or memory-management mistakes corrected.
- The exact build and test commands run.
- The real build/test results.
- Whether the framebuffer was visibly displayed in QEMU.
- Any remaining issues, especially the EL0 block-driver fault.

Be precise about what you actually verified versus what you merely inferred.

**Priority: get the existing VirtIO-MMIO GPU path working correctly on the existing ARM `virt` configuration before attempting PCI support or other display architectures.**
