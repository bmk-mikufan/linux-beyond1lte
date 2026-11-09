#!/bin/sh
# Ubuntu 24.04 (noble, arm64) root file system for the Galaxy S10, packed for Odin
# (AP slot -> userdata partition). Needs root and an arm64 host (or qemu-user binfmt).
#
#   sudo DE=desktop sh s10/build-rootfs.sh out-s10/s10-kernel-<release>.tar.gz
#
# DE=desktop  Ubuntu Desktop (GNOME, GDM), on-screen keyboard for the touchscreen (default)
# DE=lomiri   Ubuntu Touch's shell (Lomiri, Maliit keyboard) from noble's universe: experimental
# DE=server   no desktop
# Every variant has the server and USB KVM side (ssh, NetworkManager, ustreamer, pyserial).
# Output: $OUT/userdata.img (sparse ext4) and s10-ubuntu-$DE-userdata.tar.md5
#
# The image is a bare ext4 file system for the whole userdata partition (no partition table
# inside): U-Boot loads /boot/s10/Image from it, the kernel mounts it by PARTUUID, and
# systemd grows it to the partition on the first boot (x-systemd.growfs).
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
KTAR=$(realpath "$1")
TOP=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$TOP/out-s10}
DE=${DE:-desktop}
SUITE=${SUITE:-noble}
MIRROR=${MIRROR:-http://ports.ubuntu.com/ubuntu-ports}
case $DE in
server) SIZE=${SIZE:-3G} ;;
desktop|lomiri) SIZE=${SIZE:-9G} ;;
*) echo "DE must be desktop, lomiri or server" >&2; exit 2 ;;
esac
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
# no snaps: snapd cannot seed inside a chroot, and Firefox comes as a deb-less snap stub
cat > "$R/etc/apt/preferences.d/no-snap" <<EOF
Package: snapd
Pin: release a=*
Pin-Priority: -10
EOF

mount -t proc proc "$R/proc"; mount -t sysfs sys "$R/sys"; mount --bind /dev "$R/dev"
trap 'umount "$R/dev" "$R/sys" "$R/proc" 2>/dev/null || true' EXIT
cp /etc/resolv.conf "$R/etc/resolv.conf"

chroot "$R" env DE="$DE" sh -eu <<'EOF'
export DEBIAN_FRONTEND=noninteractive
apt-get update
# server basics, Wi-Fi (BCM4375 firmware comes from the phone's vendor partition), and the
# KVM side: v4l-utils/ustreamer for the capture card, python3-serial for a serial-to-HID bridge
apt-get install -y --no-install-recommends \
	systemd-resolved systemd-timesyncd netplan.io network-manager wpasupplicant iw wireless-regdb \
	openssh-server sudo bash-completion nano less vim-tiny iproute2 iputils-ping curl \
	usbutils pciutils e2fsprogs htop tmux tzdata console-setup kbd \
	v4l-utils ustreamer python3 python3-serial alsa-utils alsa-ucm-conf bluez
case $DE in
desktop)
	# GNOME shows its on-screen keyboard for touch input by itself (forced on below)
	apt-get install -y --no-install-recommends \
		ubuntu-desktop-minimal gdm3 gnome-shell gnome-session gnome-terminal nautilus \
		gnome-control-center gnome-text-editor gnome-system-monitor gnome-disk-utility \
		yaru-theme-gtk yaru-theme-icon yaru-theme-gnome-shell yaru-theme-sound \
		fonts-noto-cjk fonts-noto-color-emoji ibus ibus-hangul language-pack-ko \
		mesa-utils libgl1-mesa-dri mesa-vulkan-drivers dconf-cli xdg-user-dirs \
		network-manager-gnome gnome-keyring \
		pipewire-audio wireplumber iio-sensor-proxy vulkan-tools
	;;
lomiri)
	apt-get install -y --no-install-recommends \
		lomiri lomiri-desktop-session lomiri-system-settings lomiri-terminal-app \
		maliit-keyboard gdm3 fonts-noto-cjk language-pack-ko \
		libgl1-mesa-dri mesa-vulkan-drivers dconf-cli xdg-user-dirs \
		pipewire-audio wireplumber iio-sensor-proxy
	;;
esac
apt-get clean
locale-gen en_US.UTF-8 ko_KR.UTF-8
update-locale LANG=ko_KR.UTF-8
echo s10 > /etc/hostname
printf '127.0.0.1 localhost\n127.0.1.1 s10\n' > /etc/hosts
# first login: ubuntu / ubuntu, the password must be changed at once
useradd -m -s /bin/bash -G sudo,video,render,input,dialout,plugdev,audio ubuntu
echo ubuntu:ubuntu | chpasswd
chage -d 0 ubuntu
systemctl enable ssh NetworkManager systemd-resolved systemd-timesyncd
# a KVM has to stay awake: no system sleep from the desktop or logind
systemctl mask sleep.target suspend.target hibernate.target hybrid-sleep.target
ln -sf /run/systemd/resolve/stub-resolv.conf /etc/resolv.conf
if [ "$DE" = lomiri ]; then
	# GDM's default session for the user: whichever Lomiri session the packages ship
	s=$(cd /usr/share/wayland-sessions && ls *lomiri*.desktop 2>/dev/null | head -1)
	if [ -n "$s" ]; then
		mkdir -p /var/lib/AccountsService/users
		printf '[User]\nSession=%s\nXSession=%s\n' "${s%.desktop}" "${s%.desktop}" \
			> /var/lib/AccountsService/users/ubuntu
	fi
fi
if [ "$DE" != server ]; then
	systemctl set-default graphical.target
	# GNOME defaults: on-screen keyboard, 300% for a 550 ppi panel, never suspend
	mkdir -p /etc/dconf/profile /etc/dconf/db/local.d
	printf 'user-db:user\nsystem-db:local\n' > /etc/dconf/profile/user
	cat > /etc/dconf/db/local.d/00-s10 <<'DCONF'
[org/gnome/desktop/a11y/applications]
screen-keyboard-enabled=true

[org/gnome/desktop/interface]
scaling-factor=uint32 3

[org/gnome/settings-daemon/plugins/power]
sleep-inactive-ac-type='nothing'
sleep-inactive-battery-type='nothing'

[org/gnome/desktop/session]
idle-delay=uint32 300
DCONF
	dconf update
fi
EOF

# NetworkManager manages everything (nmcli on the console, or GNOME Settings, for Wi-Fi)
mkdir -p "$R/etc/netplan"
printf 'network:\n  version: 2\n  renderer: NetworkManager\n' > "$R/etc/netplan/01-s10.yaml"
chmod 600 "$R/etc/netplan/01-s10.yaml"

echo 'LABEL=s10root / ext4 defaults,noatime,x-systemd.growfs 0 1' > "$R/etc/fstab"

# kernel and modules
# --keep-directory-symlink: the tarball has lib/, the rootfs a /lib -> usr/lib link (usrmerge)
tar -C "$R" --keep-directory-symlink -xzf "$KTAR"
KREL=$(ls "$R/lib/modules" | head -1)
chroot "$R" depmod -a "$KREL"

# port files (firmware from vendor, touch, GPU, USB KVM tuning)
cp -a "$TOP/s10/rootfs/." "$R/"
chroot "$R" systemctl enable s10-firstboot.service s10-touch-bind.service s10-gpu-load.service \
	s10-bt-addr.service bluetooth.service

umount "$R/dev" "$R/sys" "$R/proc"; trap - EXIT
rm -f "$R/etc/resolv.conf"; ln -s /run/systemd/resolve/stub-resolv.conf "$R/etc/resolv.conf"

# ext4 that U-Boot reads: no orphan_file / csum_seed (newer than its ext4 reader)
rm -f "$OUT/rootfs.ext4"
mkfs.ext4 -q -L s10root -O ^orphan_file,^metadata_csum_seed -d "$R" "$OUT/rootfs.ext4" "$SIZE"
img2simg "$OUT/rootfs.ext4" "$OUT/userdata.img" 4096
rm -f "$OUT/rootfs.ext4"

cd "$OUT"
T=s10-ubuntu-$DE-userdata.tar
rm -f $T $T.md5
tar --format=ustar -cf $T userdata.img
md5sum -t $T >> $T
mv $T $T.md5
ls -l userdata.img $T.md5
