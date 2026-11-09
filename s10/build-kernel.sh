#!/bin/sh
# Galaxy S10 (beyond1lte) kernel for a headless Ubuntu Server / USB KVM.
# Starts from the S10e's mobian/kernel.config and only changes what a KVM needs.
# Output: $OUT/s10-kernel-<release>.tar.gz with boot/s10/Image and lib/modules/<release>.
#
#   sh s10/build-kernel.sh            # from the top of the tree; JOBS=8 by default
set -eu
cd "$(dirname "$0")/.."
OUT=${OUT:-$PWD/out-s10}
JOBS=${JOBS:-8}
M="make O=$OUT ARCH=arm64 LLVM=1 LOCALVERSION= -j$JOBS"

mkdir -p "$OUT"
cp mobian/kernel.config "$OUT/.config"
C="scripts/config --file $OUT/.config"
$C --set-str LOCALVERSION "-s10"
# KVM peripherals behind the hub: serial-to-HID bridges (CH9329 sits behind a CH340/CP2102),
# UVC capture cards with their USB audio, USB Ethernet for a wired ssh
$C --enable USB_SERIAL_CH341 --enable USB_SERIAL_CP210X --enable USB_SERIAL_FTDI_SIO \
   --enable USB_SERIAL_PL2303 --enable USB_ACM \
   --module USB_VIDEO_CLASS --enable SND_USB_AUDIO \
   --module USB_RTL8152 --module USB_NET_AX88179_178A --module USB_NET_CDC_ETHER \
   --module USB_NET_CDC_NCM --module USB_USBNET --enable USB_UAS
$M olddefconfig
$M Image modules

R=$(cat "$OUT/include/config/kernel.release")
S=$OUT/stage
rm -rf "$S"; mkdir -p "$S/boot/s10"
cp "$OUT/arch/arm64/boot/Image" "$S/boot/s10/Image"
$M INSTALL_MOD_PATH="$S" INSTALL_MOD_STRIP=1 modules_install
rm -f "$S/lib/modules/$R/build" "$S/lib/modules/$R/source"
cp "$OUT/.config" "$S/boot/s10/config-$R"
tar -C "$S" -czf "$OUT/s10-kernel-$R.tar.gz" boot lib
sha256sum "$OUT/s10-kernel-$R.tar.gz" | tee "$OUT/s10-kernel-$R.tar.gz.sha256"
echo "kernel $R -> $OUT/s10-kernel-$R.tar.gz"
