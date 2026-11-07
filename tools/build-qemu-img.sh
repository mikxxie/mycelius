#!/bin/sh
# build-qemu-img.sh — build initramfs + compile Linux kernel for QEMU aarch64 virt
# Output: OUTDIR/mycelius-initramfs.gz and OUTDIR/mycelius-kernel
set -eu

OUTDIR="${1:-/tmp}"
mkdir -p "$OUTDIR"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
XNU_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_ROOT="$(mktemp -d)"

cleanup() { rm -rf "$BUILD_ROOT"; }
trap cleanup EXIT

INITRAMFS_OUT="/tmp/mycelius-initramfs.gz"
KERNEL_OUT="/tmp/mycelius-kernel"
CC="aarch64-linux-gnu-gcc"

echo "==> Building mycelius daemons (aarch64-linux-gnu, static)"
make -C "$XNU_ROOT/linux/drivers/mycelius" clean >/dev/null 2>&1 || true
make -C "$XNU_ROOT/linux/drivers/mycelius" \
    CC="$CC" \
    CFLAGS="-O2 -Wall -Wextra -std=c11 -I. -static"

echo "==> Compiling static init (aarch64-linux-gnu)"
mkdir -p "$XNU_ROOT/rootfs"
aarch64-linux-gnu-gcc -O2 -Wall -std=c11 -static \
    -o "$XNU_ROOT/rootfs/init" "$XNU_ROOT/tools/init.c"

echo "==> Staging rootfs for initramfs"
rm -rf "$BUILD_ROOT/rootfs"
mkdir -p "$BUILD_ROOT/rootfs/sbin" "$BUILD_ROOT/rootfs/proc" "$BUILD_ROOT/rootfs/sys" "$BUILD_ROOT/rootfs/dev" "$BUILD_ROOT/rootfs/etc" "$BUILD_ROOT/rootfs/tmp" "$BUILD_ROOT/rootfs/var/log" "$BUILD_ROOT/rootfs/var/run"
cp "$XNU_ROOT/linux/drivers/mycelius/backboardd" "$BUILD_ROOT/rootfs/sbin/backboardd"
cp "$XNU_ROOT/linux/drivers/mycelius/launchpad" "$BUILD_ROOT/rootfs/sbin/launchpad"
cp "$XNU_ROOT/linux/drivers/mycelius/init" "$BUILD_ROOT/rootfs/sbin/init"
cp "$XNU_ROOT/linux/drivers/mycelius/mycc" "$BUILD_ROOT/rootfs/sbin/mycc"
cp "$XNU_ROOT/linux/drivers/mycelius/ignitectl" "$BUILD_ROOT/rootfs/sbin/ignitectl"
cp "$XNU_ROOT/linux/drivers/mycelius/discoveryd" "$BUILD_ROOT/rootfs/sbin/discoveryd"
cp "$XNU_ROOT/linux/drivers/mycelius/updatesd" "$BUILD_ROOT/rootfs/sbin/updatesd"
cp "$XNU_ROOT/linux/drivers/mycelius/moad" "$BUILD_ROOT/rootfs/sbin/moad"
chmod 0755 "$BUILD_ROOT/rootfs/sbin/"*

echo "==> Creating initramfs"
cd "$BUILD_ROOT/rootfs"
find . -mindepth 1 -printf '%P\0' | cpio --null -o -H newc | gzip > "$INITRAMFS_OUT"

echo "==> Building Linux kernel (aarch64) from our tree"
KERNEL_DIR="$XNU_ROOT/linux"
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig >/dev/null 2>&1
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc) Image >/dev/null 2>&1
cp arch/arm64/boot/Image "$KERNEL_OUT"

if [ ! -f "$KERNEL_OUT" ]; then
    echo "   ERROR: Could not build kernel. Place an aarch64 kernel at:"
    echo "     $KERNEL_OUT"
    touch "$OUTDIR/NO_KERNEL"
fi

echo "==> Done:"
ls -lh "$INITRAMFS_OUT" "$KERNEL_OUT" 2>/dev/null || true
