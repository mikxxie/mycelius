#!/bin/sh
# build-qemu-img.sh — build initramfs + download kernel for QEMU aarch64 virt
# Output: /tmp/mycelius-initramfs.gz and /tmp/mycelius-kernel
#
# Usage: ./build-qemu-img.sh [output-dir]
set -eu

OUTDIR="${1:-/tmp}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
XNU_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOTFS_DIR="$XNU_ROOT/rootfs"
BUILD_ROOT="$(mktemp -d)"

cleanup() { rm -rf "$BUILD_ROOT"; }
trap cleanup EXIT

echo "==> Building mycelius daemons (aarch64-linux-gnu, static)"
make -C "$XNU_ROOT/usr.sbin/backboardd" CC=aarch64-linux-gnu-gcc \
    CFLAGS="-O2 -Wall -std=c11 -I../include -static" >/dev/null 2>&1
make -C "$XNU_ROOT/usr.sbin/launchpad" CC=aarch64-linux-gnu-gcc \
    CFLAGS="-O2 -Wall -std=c11 -I../include -static" >/dev/null 2>&1

echo "==> Staging rootfs for initramfs"
rm -rf "$BUILD_ROOT/rootfs"
mkdir -p "$BUILD_ROOT/rootfs"
cp -a "$ROOTFS_DIR/." "$BUILD_ROOT/rootfs/"
chmod +x "$BUILD_ROOT/rootfs/init"
cp "$XNU_ROOT/usr.sbin/backboardd/backboardd" "$BUILD_ROOT/rootfs/sbin/backboardd"
cp "$XNU_ROOT/usr.sbin/launchpad/launchpad" "$BUILD_ROOT/rootfs/sbin/launchpad"
chmod 0755 "$BUILD_ROOT/rootfs/sbin/backboardd" "$BUILD_ROOT/rootfs/sbin/launchpad"

echo "==> Creating initramfs"
cd "$BUILD_ROOT/rootfs"
find . -print0 | cpio --null -o -H newc | gzip > "$OUTDIR/mycelius-initramfs.gz"

echo "==> Checking for kernel at $OUTDIR/mycelius-kernel"
if [ ! -f "$OUTDIR/mycelius-kernel" ]; then
    echo "   (no kernel cached — QEMU will need -kernel pointed at an aarch64 vmlinux)"
    touch "$OUTDIR/NO_KERNEL"
fi

echo "==> Done:"
ls -lh "$OUTDIR/mycelius-initramfs.gz" "$OUTDIR/mycelius-kernel" 2>/dev/null || true
