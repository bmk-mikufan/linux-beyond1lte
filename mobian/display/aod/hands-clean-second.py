#!/usr/bin/env python3
# hands-clean-second.py IN OUT line|none [PREVIEW.png]: the self clock's hand image with a plainer second hand, or
# none. Samsung's (hands-from-samsung.py) has a counterweight ring 20 px behind the pivot and a stub past it, which
# read as a second, wandering centre. This keeps its hour and minute hands; "line" draws the second hand as one
# line through the pivot (a 20 px tail, its colour 250 and its reach x 297), "none" leaves its band transparent
# (the owner's choice: hour and minute are enough for an AOD). Bands are 360x40, pointing right, pivot (90, 20) in
# pixel-edge coordinates (the DDI's 5A 14); RGBA, transparent = 255 255 255 0, as Samsung's.
# PREVIEW renders dial + hands at 10:08:37 against the dial Phosh draws (scale 3), to check the geometry.
import math, struct, sys, zlib

W, H, BAND = 360, 120, 40
PX, PY = 90.0, 20.0
TAIL, TIP, HALF = 70.0, 295.5, 2.5   # the segment and half its width; round ends
GREY = 250

src = bytearray(open(sys.argv[1], 'rb').read())
assert len(src) == W * H * 4, len(src)
mode = sys.argv[3]
assert mode in ('line', 'none'), mode

for y in range(2 * BAND, 3 * BAND):
    for x in range(W):
        cx, cy = x + 0.5, y - 2 * BAND + 0.5
        dx = cx - min(max(cx, TAIL), TIP)
        d = math.hypot(dx, cy - PY)
        a = min(max(HALF + 0.5 - d, 0.0), 1.0) if mode == 'line' else 0.0
        o = (y * W + x) * 4
        src[o:o + 4] = bytes((GREY, GREY, GREY, round(a * 255))) if a > 0 else b'\xff\xff\xff\x00'
open(sys.argv[2], 'wb').write(src)

if len(sys.argv) > 4:
    # the dial as Phosh's dial_draw_cb draws it, in panel pixels, plus the three hands rotated about the pivot
    S = 520
    img = [[0.0] * S for _ in range(S)]
    c = S / 2
    def stroke(x0, y0, x1, y1, w, v):
        for y in range(S):
            for x in range(S):
                px, py = x + 0.5 - x0, y + 0.5 - y0
                vx, vy = x1 - x0, y1 - y0
                t = max(0, min(1, (px * vx + py * vy) / (vx * vx + vy * vy)))
                d = math.hypot(px - t * vx, py - t * vy)
                a = min(max(w / 2 + 0.5 - d, 0), 1)
                img[y][x] = max(img[y][x], a * v)
    for i in range(12):
        a = i * math.pi / 6
        inner = 218 if i % 3 else 204
        stroke(c + inner * math.sin(a), c - inner * math.cos(a), c + 236 * math.sin(a), c - 236 * math.cos(a),
               6 if i % 3 == 0 else 3, 0x6a / 255)
    for y in range(S):
        for x in range(S):
            if math.hypot(x + 0.5 - c, y + 0.5 - c) <= 9:
                img[y][x] = max(img[y][x], 0x6a / 255)
    h, m, s = 10, 8, 37
    for band, ang in ((0, (h % 12 + m / 60) * 30), (1, (m + s / 60) * 6), (2, s * 6)):
        th = math.radians(ang - 90)   # 0 deg = 12 o'clock; the band points right (3 o'clock)
        for y in range(S):
            for x in range(S):
                rx, ry = x + 0.5 - c, y + 0.5 - c
                bx = rx * math.cos(th) + ry * math.sin(th) + PX
                by = -rx * math.sin(th) + ry * math.cos(th) + PY
                ix, iy = int(math.floor(bx)), int(math.floor(by))
                if 0 <= ix < W and 0 <= iy < BAND:
                    o = ((band * BAND + iy) * W + ix) * 4
                    al = src[o + 3] / 255
                    if al:
                        img[y][x] = img[y][x] * (1 - al) + al * src[o] / 255
    raw = b''.join(b'\x00' + bytes(round(v * 255) for v in row) for row in img)
    def chunk(t, b):
        return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
    open(sys.argv[4], 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', S, S, 8, 0, 0, 0, 0)) +
                                  chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))
