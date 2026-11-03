# s10e-sleepd: `make` builds for the dev box, `make arm64` for the phone (SR = an arm64 Mobian sysroot)
SR ?= /path/to/arm64-sysroot
W = -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
    -DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_74 -DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_74

s10e-sleepd: s10e-sleepd.c
	$(CC) $(W) $$(pkg-config --cflags gio-2.0 gio-unix-2.0) $< -o $@ $$(pkg-config --libs gio-2.0 gio-unix-2.0)

arm64: s10e-sleepd.arm64
s10e-sleepd.arm64: s10e-sleepd.c
	PKG_CONFIG_LIBDIR=$(SR)/usr/lib/aarch64-linux-gnu/pkgconfig:$(SR)/usr/share/pkgconfig PKG_CONFIG_SYSROOT_DIR=$(SR) sh -c \
	'clang --target=aarch64-linux-gnu --sysroot=$(SR) -fuse-ld=lld $(W) $$(pkg-config --cflags gio-2.0 gio-unix-2.0) $< -o $@ $$(pkg-config --libs gio-2.0 gio-unix-2.0)'

clean:
	rm -f s10e-sleepd s10e-sleepd.arm64
.PHONY: arm64 clean
