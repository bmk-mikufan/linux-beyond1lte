#!/usr/bin/env python3
# hands-from-samsung.py HEADER OUT: the S6E3FA7 self clock's hand image (360x120 RGBA, 172800 B) out of Samsung's
# GPL kernel header drivers/video/fbdev/exynos/panel/s6e3fa7/s6e3fa7_analog_clock_img.h, for
# /lib/firmware/samsung/s6e3fa7-beyond0-aod-hands.bin (panel-samsung-s6e3fa7-beyond0's self_clock).
import re, sys
src = open(sys.argv[1]).read()
body = src[src.index('S6E3FA7_ANALOG_IMG'):]
body = body[body.index('{') + 1:body.index('}')]
data = bytes(int(h, 16) for h in re.findall(r'0x([0-9A-Fa-f]{2})', body))
assert len(data) == 360 * 120 * 4, len(data)
open(sys.argv[2], 'wb').write(data)
