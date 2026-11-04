#!/bin/sh
# build-qemu-img.sh — build initramfs + compile Linux kernel for QEMU aarch64 virt
# Output: OUTDIR/mycelius-initramfs.gz and OUTDIR/mycelius-kernel
set -eu

OUTDIR="${1:-/tmp}"
mkdir -p "$OUTDIR"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
XNU_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOTFS_DIR="$XNU_ROOT/rootfs"
BUILD_ROOT="$(mktemp -d)"

cleanup() { rm -rf "$BUILD_ROOT"; }
trap cleanup EXIT

INITRAMFS_OUT="/tmp/mycelius-initramfs.gz"
KERNEL_OUT="/tmp/mycelius-kernel"

echo "==> Building mycelius daemons (aarch64-linux-gnu, static)"
make -C "$XNU_ROOT/usr.sbin/backboardd" CC=aarch64-linux-gnu-gcc \
    CFLAGS="-O2 -Wall -std=c11 -I../include -static" >/dev/null 2>&1
make -C "$XNU_ROOT/usr.sbin/launchpad" CC=aarch64-linux-gnu-gcc \
    CFLAGS="-O2 -Wall -std=c11 -I../include -static" >/dev/null 2>&1

echo "==> Compiling static init (aarch64-linux-gnu)"
mkdir -p "$XNU_ROOT/rootfs"
aarch64-linux-gnu-gcc -O2 -Wall -std=c11 -static \
    -o "$XNU_ROOT/rootfs/init" "$XNU_ROOT/tools/init.c"

echo "==> Staging rootfs for initramfs"
rm -rf "$BUILD_ROOT/rootfs"
mkdir -p "$BUILD_ROOT/rootfs/sbin" "$BUILD_ROOT/rootfs/proc" "$BUILD_ROOT/rootfs/sys" "$BUILD_ROOT/rootfs/dev" "$BUILD_ROOT/rootfs/etc" "$BUILD_ROOT/rootfs/tmp" "$BUILD_ROOT/rootfs/var/log" "$BUILD_ROOT/rootfs/var/run"
cp -a "$ROOTFS_DIR/." "$BUILD_ROOT/rootfs/"
cp "$XNU_ROOT/usr.sbin/backboardd/backboardd" "$BUILD_ROOT/rootfs/sbin/backboardd"
cp "$XNU_ROOT/usr.sbin/launchpad/launchpad" "$BUILD_ROOT/rootfs/sbin/launchpad"
chmod 0755 "$BUILD_ROOT/rootfs/sbin/backboardd" \
           "$BUILD_ROOT/rootfs/sbin/launchpad" \
           "$BUILD_ROOT/rootfs/init"

echo "==> Creating initramfs"
cd "$BUILD_ROOT/rootfs"
find . -mindepth 1 -printf '%P\0' | cpio --null -o -H newc | gzip > "$INITRAMFS_OUT"

echo "==> Ensuring kernel at $KERNEL_OUT"
if [ ! -f "$KERNEL_OUT" ]; then
    echo "   Downloading/building aarch64 Linux kernel for QEMU virt..."
    KERNEL_DIR="/tmp/linux"
    if [ ! -d "$KERNEL_DIR/.git" ]; then
        rm -rf "$KERNEL_DIR"
        git clone --depth 1 https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git "$KERNEL_DIR"
    fi
    cd "$KERNEL_DIR"
    git pull --depth 1 || true
    make ARCH=arm64 defconfig >/dev/null 2>&1
    make ARCH=arm64 -j$(nproc) Image >/dev/null 2>&1
    cp arch/arm64/boot/Image "$KERNEL_OUT"
fi

if [ ! -f "$KERNEL_OUT" ]; then
    echo "   ERROR: Could not build kernel. Place an aarch64 kernel at:"
    echo "     $KERNEL_OUT"
    touch "$OUTDIR/NO_KERNEL"
fi

echo "==> Done:"
ls -lh "$INITRAMFS_OUT" "$KERNEL_OUT" 2>/dev/null || true
