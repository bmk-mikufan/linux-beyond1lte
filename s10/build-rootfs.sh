#!/bin/sh
# Ubuntu Server (noble, arm64) root file system for the Galaxy S10, packed for Odin
# (AP slot -> userdata partition). Needs root and an arm64 host (or qemu-user binfmt).
#
#   sudo sh s10/build-rootfs.sh out-s10/s10-kernel-<release>.tar.gz
# Output: $OUT/userdata.img (sparse ext4) and s10-ubuntu-userdata.tar.md5
#
# The image is a bare ext4 file system for the whole userdata partition (no partition table
# inside): U-Boot loads /boot/s10/Image from it, the kernel mounts it by PARTUUID, and
# systemd grows it to the partition on the first boot (x-systemd.growfs).
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
KTAR=$(realpath "$1")
TOP=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$TOP/out-s10}
SUITE=${SUITE:-noble}
MIRROR=${MIRROR:-http://ports.ubuntu.com/ubuntu-ports}
SIZE=${SIZE:-3G}
R=$OUT/rootfs
mkdir -p "$OUT"
rm -rf "$R"

debootstrap --arch=arm64 --variant=minbase \
	--include=systemd-sysv,udev,kmod,dbus,apt-utils,ca-certificates,locales \
	"$SUITE" "$R" "$MIRROR"

cat > "$R/etc/apt/sources.list" <<EOF
deb $MIRROR $SUITE main restricted universe multiverse
deb $MIRROR $SUITE-updates main restricted universe multiverse
deb $MIRROR $SUITE-security main restricted universe multiverse
EOF

mount -t proc proc "$R/proc"; mount -t sysfs sys "$R/sys"; mount --bind /dev "$R/dev"
trap 'umount "$R/dev" "$R/sys" "$R/proc" 2>/dev/null || true' EXIT
cp /etc/resolv.conf "$R/etc/resolv.conf"

chroot "$R" sh -eu <<'EOF'
export DEBIAN_FRONTEND=noninteractive
apt-get update
# server basics, Wi-Fi (BCM4375 firmware comes from the phone's vendor partition), and the
# KVM side: v4l-utils/ustreamer for the capture card, python3-serial for a serial-to-HID bridge
apt-get install -y --no-install-recommends \
	systemd-resolved systemd-timesyncd netplan.io network-manager wpasupplicant iw wireless-regdb \
	openssh-server sudo bash-completion nano less vim-tiny iproute2 iputils-ping curl \
	usbutils pciutils e2fsprogs htop tmux tzdata console-setup kbd \
	v4l-utils ustreamer python3 python3-serial alsa-utils
apt-get clean
locale-gen en_US.UTF-8 ko_KR.UTF-8
update-locale LANG=en_US.UTF-8
echo s10 > /etc/hostname
printf '127.0.0.1 localhost\n127.0.1.1 s10\n' > /etc/hosts
# first login: ubuntu / ubuntu, the password must be changed at once
useradd -m -s /bin/bash -G sudo,video,dialout,plugdev,audio ubuntu
echo ubuntu:ubuntu | chpasswd
chage -d 0 ubuntu
systemctl enable ssh NetworkManager systemd-resolved systemd-timesyncd
ln -sf /run/systemd/resolve/stub-resolv.conf /etc/resolv.conf
EOF

# NetworkManager manages everything (nmcli on the console for Wi-Fi)
mkdir -p "$R/etc/netplan"
printf 'network:\n  version: 2\n  renderer: NetworkManager\n' > "$R/etc/netplan/01-s10.yaml"
chmod 600 "$R/etc/netplan/01-s10.yaml"

echo 'LABEL=s10root / ext4 defaults,noatime,x-systemd.growfs 0 1' > "$R/etc/fstab"

# kernel and modules
tar -C "$R" -xzf "$KTAR"
KREL=$(ls "$R/lib/modules" | head -1)
chroot "$R" depmod -a "$KREL"

# port files (firmware from vendor, USB KVM tuning)
cp -a "$TOP/s10/rootfs/." "$R/"
chroot "$R" systemctl enable s10-firstboot.service

umount "$R/dev" "$R/sys" "$R/proc"; trap - EXIT
rm -f "$R/etc/resolv.conf"; ln -s /run/systemd/resolve/stub-resolv.conf "$R/etc/resolv.conf"

# ext4 that U-Boot reads: no orphan_file / csum_seed (newer than its ext4 reader)
rm -f "$OUT/rootfs.ext4"
mkfs.ext4 -q -L s10root -O ^orphan_file,^metadata_csum_seed -d "$R" "$OUT/rootfs.ext4" "$SIZE"
img2simg "$OUT/rootfs.ext4" "$OUT/userdata.img" 4096
rm -f "$OUT/rootfs.ext4"

cd "$OUT"
T=s10-ubuntu-userdata.tar
rm -f $T $T.md5
tar --format=ustar -cf $T userdata.img
md5sum -t $T >> $T
mv $T $T.md5
ls -l userdata.img $T.md5
