#!/bin/sh
# Install s10e-sleepd on the S10e: gnome/device/s10e-sleep/install.sh [root@host]   (make arm64 first)
# SSH="..." replaces "ssh root@host" (a per-boot host key wants its own options)
set -eu
H=${1:?usage: install.sh root@host}
SSH=${SSH:-ssh $H}
cd "$(dirname "$0")"
$SSH 'cat > /tmp/s10e-sleepd.new' < s10e-sleepd.arm64
$SSH 'cat > /tmp/s10e-sleepd.service' < s10e-sleepd.service
$SSH 'cat > /tmp/s10e-sleepd.conf' < s10e-sleepd.conf
$SSH 'cat > /tmp/s10e-sleep-README.md' < README.md
$SSH 'set -e
install -m755 /tmp/s10e-sleepd.new /usr/local/sbin/s10e-sleepd
install -m644 /tmp/s10e-sleepd.service /etc/systemd/system/s10e-sleepd.service
[ -e /etc/s10e-sleepd.conf ] || install -m644 /tmp/s10e-sleepd.conf /etc/s10e-sleepd.conf
install -Dm644 /tmp/s10e-sleep-README.md /usr/local/share/doc/s10e-sleep/README.md
rm -f /tmp/s10e-sleepd.new /tmp/s10e-sleepd.service /tmp/s10e-sleepd.conf /tmp/s10e-sleep-README.md
# the laptop policy: gsd-power would suspend through logind after 15 idle minutes (its suspend targets stay masked)
for k in sleep-inactive-battery-type sleep-inactive-ac-type; do
	systemd-run --user -M user@ --wait -q gsettings set org.gnome.settings-daemon.plugins.power $k nothing || true
done
systemctl daemon-reload
systemctl enable s10e-sleepd.service >/dev/null 2>&1
systemctl restart s10e-sleepd.service
echo installed'
