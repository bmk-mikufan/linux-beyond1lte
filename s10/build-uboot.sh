#!/bin/sh
# U-Boot for the Galaxy S10 (beyond1lte), packed for Odin (AP slot -> boot partition).
#
# chiffathefox/u-boot (exynos9820) + the S10e's mobian/uboot/u-boot-s10e.patch (the shared
# Exynos 9820 DT and driver work), then the S10 device tree and environment from s10/uboot/.
# U-Boot's DT is the one Linux gets, so this DT is the kernel's too.
#
# Boot image, as S-Boot of the S10 family wants it (gangg111/S10Plus_Linux, TWRP beyond1lte):
# header v1, 2048-byte pages, base 0x10000000, and the SEANDROIDENFORCE tail after the image.
# Odin reads ustar archives only; the file name inside picks the partition; the .md5 is the tar
# with its md5sum line appended.
#
#   CROSS_COMPILE=aarch64-linux-gnu- sh s10/build-uboot.sh     (empty on an arm64 host)
# Output: $OUT/u-boot-s10.bin, boot.img, s10-uboot-boot.tar.md5
set -eu
TOP=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$TOP/out-s10}
UB=${UB:-$OUT/u-boot}
UBOOT_REPO=${UBOOT_REPO:-https://github.com/chiffathefox/u-boot.git}
UBOOT_REV=${UBOOT_REV:-dec4927b4ad5b789b0854050096fc80250627de7}
JOBS=${JOBS:-$(nproc)}
mkdir -p "$OUT"

if [ ! -d "$UB/.git" ]; then
	git clone --filter=blob:none -b exynos9820 "$UBOOT_REPO" "$UB"
fi
cd "$UB"
git checkout -q -f "$UBOOT_REV"
git clean -qfdx
git apply "$TOP/mobian/uboot/u-boot-s10e.patch"
D=dts/upstream/src/arm64/exynos
cp "$TOP/s10/uboot/exynos9820-beyond1lte.dts" $D/exynos9820-beyond1lte.dts
cp "$TOP/s10/uboot/exynos-mobile.env" board/samsung/exynos-mobile/exynos-mobile.env
# the S10 is the target: make its DT the default (the FIT still carries the others)
sed -i 's|^CONFIG_DEFAULT_DEVICE_TREE=.*|CONFIG_DEFAULT_DEVICE_TREE="exynos/exynos9820-beyond1lte"|' configs/exynos9820_defconfig

make O=.output exynos9820_defconfig
make O=.output -j"$JOBS"
cp .output/u-boot.bin "$OUT/u-boot-s10.bin"
# the Linux DT, for checking what the kernel gets
cp .output/dts/upstream/src/arm64/exynos/exynos9820-beyond1lte.dtb "$OUT/" 2>/dev/null ||
	find .output -name 'exynos9820-beyond1lte.dtb' -exec cp {} "$OUT/" \;

cd "$OUT"
rm -f boot.img
# header values as in the SM-G973N SKC G973NKSU7HXJ2 stock boot.img (board SRPRI28D007KU,
# 12.0.0 / 2023-04, kernel at 0x10008000, tags at 0x10000100, 2048-byte pages)
python3 "$TOP/s10/mkbootimg-v1.py" u-boot-s10.bin boot.img \
	--base 0x10000000 --pagesize 2048 --board "${BOARD:-SRPRI28D007KU}" \
	--os-version 12.0.0 --os-patch-level 2023-04
printf 'SEANDROIDENFORCE' >> boot.img

T=s10-uboot-boot.tar
rm -f $T $T.md5
tar --format=ustar -cf $T boot.img
md5sum -t $T >> $T
mv $T $T.md5
ls -l u-boot-s10.bin boot.img $T.md5
