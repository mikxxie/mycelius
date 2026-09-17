# Mycelis agent handoff

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
  - Strict reboot persistence verified across QEMU restarts and confirmed 100% clean
    by host `e2fsck`.
- Shell VFS routes to ext4 when mounted, and falls back to RAM VFS when running
  without disk.
- **Project renamed from `mycelis` to `mycelius`** — all output artifacts,
  source files, and directory renamed accordingly.
- **Git repo initialized** at project root — always `git add`/`git commit`
  after batches of changes so work can be recovered; never delete history.
- MNU (Mycelius New Utility) CLI command implemented and verified in QEMU:
  - `mnu` opens an editor prompt: welcome message, save/exit instructions,
    and a `>` prompt instead of `mycelius>`.
  - `mnu <name>` (e.g. `mnu file.txt`) echoes `File: file.txt` on entry.
  - Ctrl+S (0x13) saves and prints `[<name>] saved`, or `[New File] saved`
    when no name was given.
  - Ctrl+X (0x24) exits; pressing it again confirms when changes are unsaved
    (shows `Save changes before exit? y/n`).
  - Editor text is currently an in-memory buffer in `cli.c`; not yet
    persisted to ext4.

## Next work, in required order

1. Proceed with the virtio-net queue, Ethernet framing, ARP, IPv4 and ICMP.
2. Build per-task EL0 translation tables and transition the drivers into isolated
   EL0 user processes.
3. Verify MNU CLI displays correctly and ext4 ls/persistence works across reboots.
4. Consider persisting MNU editor buffer to ext4 on Ctrl+S.
