# Mycelius agent handoff

Project overview, build/run instructions, and repo layout: see INSTRUCTIONS.md.

## Project constraints

- AArch64 QEMU `virt`, hard 16 MiB RAM limit, freestanding EL1 core.
- Drivers are intended to become EL0 processes; current virtio transport is
  still bring-up code in the kernel and must not be presented as final isolation.
- Preserve `mycelius.img`; `make format-disk` is explicitly destructive.

## Current working state

- `make run` provides virtio-net and falls back gracefully to the RAM VFS.
- `make run-disk` attaches `mycelius.img` as virtio-blk and mounts ext4 read-write.
- Virtio queue completion accounting tracks `used_idx` and `used_seen` correctly.
- Synchronous bounded sector read, write, and flush paths (`virtio_blk_read`,
  `virtio_blk_write`, `virtio_blk_flush`) are fully operational.
- ext4 driver supports:
  - Superblock, group descriptor, inode table, and extent tree leaf traversal.
  - Full metadata checksum calculations (CRC32C) for directory block tails, inodes,
    block bitmaps, inode bitmaps, group descriptors, and the superblock.
  - Single and recursive / path-based directory creation (`mkdir dir/home/hello/lol`).
  - Path navigation (`cd`, `pwd`, `ls`, `ls <path>`).
  - Regular-file creation and data write (`ext4_write_file`) modeled on `mkdir`:
    inode + one 1024-byte data block allocated from the bitmaps, extent-mapped inode
    (mode 0644, `EXT4_EXTENTS_FL`), directory entry insertion, group descriptor and
    superblock count/checksum updates, then `virtio_blk_flush`.
  - In-place overwrite of existing regular files on re-save (size/mtime updated,
    blockcount kept at 2 sectors as total disk usage).
  - File read-back (`ext4_cat`) — resolves regular files and prints contents,
    rendering stored `\n` as `\r\n` for the serial console.
  - Strict reboot persistence verified across QEMU restarts and confirmed 100% clean
    by host `e2fsck`.
- Shell VFS routes to ext4 when mounted, and falls back to RAM VFS when running
  without disk.
- virtio-net stack (legacy transport, verified in QEMU):
  - MMIO discovery, MAC read from config space, rx queue 0 + tx queue 1.
  - Synchronous `virtio_net_rx` / `virtio_net_tx` with correct used-ring
    accounting (per-completion ring element, not just ring[0]).
  - Ethernet framing, ARP (request/reply + small cache), IPv4, ICMP echo.
  - `net ping <ip>` resolves via ARP then ICMP echo; guest→host ping to
    10.0.2.2 verified end-to-end. Host→guest depends on slirp ARP behaviour.
- **Project renamed from `mycelis` to `mycelius`** — all output artifacts,
  source files, and directory renamed accordingly.
- **Git repo initialized** at project root — always `git add`/`git commit`
  after batches of changes so work can be recovered; never delete history.
- MNU (Mycelius New Utility) CLI command implemented and verified in QEMU:
  - `mnu` opens an editor prompt: welcome message, save/exit instructions,
    and a `>` prompt instead of `mycelius>`.
  - `mnu <name>` (e.g. `mnu file.txt`) echoes `File: file.txt` on entry.
  - Editor keys: printable chars and space append to a 1024-byte in-memory
    buffer (`mnu_buf`), Enter inserts a newline, backspace erases.
  - Ctrl+S (0x13) persists the buffer to ext4 via `vfs_write_file` and prints
    `[<name>] saved`, or `[New File] saved` when no name was given (written to
    `newfile.txt`). Save failure prints `save failed`. Save/exit messages start
    on a new line even when the buffer has no trailing newline; save keeps the
    editor open (`>` prompt).
  - Ctrl+X (0x24) exits. If there are unsaved changes it asks `Save changes
    before exit? y/n` and waits: `y` saves then exits, `n` discards, and a
    second Ctrl+X also confirms quit. Unsaved state is only tracked after the
    buffer is actually edited (typing/Enter/backspace).
  - `cat <file>` command prints a saved file's contents (`vfs_cat` → `ext4_cat`).

## Next work, in required order

1. ~~Proceed with the virtio-net queue, Ethernet framing, ARP, IPv4 and ICMP.~~
   Done — see the virtio-net stack notes above.
2. ~~Build per-task EL0 translation tables and transition the drivers into isolated EL0 user processes.~~
   Page tables and task_launch exist; EL0 driver execution (bring-up) pending.
3. ~~Development headers / syscall surface~~ - Done — see dev/ directory.
   C/C++ headers: dev/ipc.h, dev/net.h, dev/drv.h, dev/fs.h, dev/syscall.h,
   plus C++ wrappers (*.hpp) and umbrella header dev/mycelius.h.
4. Build per-task EL0 translation tables and transition drivers into isolated
   EL0 user processes (bring-up: net driver at EL0 hangs on first SVC — debug
   needed).
5. GUI (see INSTRUCTIONS.md roadmap).
