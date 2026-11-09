# Say it out loud when the desktop is not on the GPU (s10-gpu-load found G3D off or
# panfrost failed): then GNOME/Mir draw with llvmpipe.
if [ -e /sys/class/drm/card0 ] && [ ! -e /dev/dri/renderD128 ]; then
	echo "s10: GPU (panfrost) not up, software rendering. See: journalctl -u s10-gpu-load" >&2
fi
