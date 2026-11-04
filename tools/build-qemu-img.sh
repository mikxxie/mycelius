#!/bin/sh
# build-qemu-img.sh — build a bootable initramfs + download kernel for QEMU aarch64 virt
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
make -C "$XNU_ROOT/usr.sbin/backboardd" cross CC=aarch64-linux-gnu-gcc >/dev/null 2>&1
make -C "$XNU_ROOT/usr.sbin/launchpad" cross CC=aarch64-linux-gnu-gcc >/dev/null 2>&1

echo "==> Compiling static init (aarch64-linux-gnu)"
aarch64-linux-gnu-gcc -O2 -Wall -std=c11 -static \
    -o "$XNU_ROOT/rootfs/init" "$XNU_ROOT/tools/init.c"

echo "==> Staging rootfs for initramfs"
rm -rf "$BUILD_ROOT/rootfs"
mkdir -p "$BUILD_ROOT/rootfs"
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
    echo "   Downloading aarch64 Linux kernel for QEMU virt..."
    KERNEL_DEB="https://kernel.ubuntu.com/~kernel-ppa/mainline/daily/current/arm64/linux-image-unsigned-7.3.0-070300daily20261101-generic_7.3.0-070300daily202611010203_arm64.deb"
    if curl -f -sSL --max-time 120 -o /tmp/mycelius-kernel.deb "$KERNEL_DEB" 2>/dev/null; then
        echo "   Extracting kernel from deb..."
        mkdir -p /tmp/kernel-extract
        dpkg-deb -x /tmp/mycelius-kernel.deb /tmp/kernel-extract 2>/dev/null || true
        if [ -f /tmp/kernel-extract/boot/vmlinuz-* ]; then
            cp /tmp/kernel-extract/boot/vmlinuz-* "$KERNEL_OUT"
        elif [ -f /tmp/kernel-extract/boot/vmlinux-* ]; then
            cp /tmp/kernel-extract/boot/vmlinux-* "$KERNEL_OUT"
        fi
        rm -rf /tmp/kernel-extract /tmp/mycelius-kernel.deb
    fi
fi

if [ ! -f "$KERNEL_OUT" ]; then
    echo "   WARNING: No kernel downloaded. Place an aarch64 kernel at:"
    echo "     $KERNEL_OUT"
    touch "$OUTDIR/NO_KERNEL"
fi

echo "==> Done:"
ls -lh "$INITRAMFS_OUT" "$KERNEL_OUT" 2>/dev/null || true
