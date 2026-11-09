#!/usr/bin/env python3
"""Android boot image, header v1, kernel only: what S-Boot of the Galaxy S10 family loads.

Ubuntu noble's mkbootimg package is broken (ModuleNotFoundError: gki), and this is all of
it that is needed. Layout as AOSP mkbootimg.py v1 (and mobian/uboot/repack.py, which reads
it back): 1648-byte header in one page, then the kernel, page-aligned. No ramdisk, second
stage or recovery_dtbo.

  mkbootimg-v1.py KERNEL OUT [--base 0x10000000] [--pagesize 2048] [--board NAME]
"""
import argparse, hashlib, struct

ap = argparse.ArgumentParser()
ap.add_argument('kernel'); ap.add_argument('out')
ap.add_argument('--base', type=lambda s: int(s, 0), default=0x10000000)
ap.add_argument('--kernel-offset', type=lambda s: int(s, 0), default=0x00008000)
ap.add_argument('--ramdisk-offset', type=lambda s: int(s, 0), default=0x01000000)
ap.add_argument('--second-offset', type=lambda s: int(s, 0), default=0x00f00000)
ap.add_argument('--tags-offset', type=lambda s: int(s, 0), default=0x00000100)
ap.add_argument('--pagesize', type=int, default=2048)
ap.add_argument('--board', default='')
ap.add_argument('--os-version', default='12.0.0')
ap.add_argument('--os-patch-level', default='2023-01')
a = ap.parse_args()

kernel = open(a.kernel, 'rb').read()
x, y, z = (int(v) for v in a.os_version.split('.'))
yy, mm = (int(v) for v in a.os_patch_level.split('-')[:2])
os_version = ((x << 14) | (y << 7) | z) << 11 | ((yy - 2000) << 4) | mm

# id: SHA-1 over each part and its size, recovery_dtbo included from v1 on (AOSP)
sha = hashlib.sha1()
for part in (kernel, b'', b'', b''):	# kernel, ramdisk, second, recovery_dtbo
	sha.update(part); sha.update(struct.pack('<I', len(part)))

hdr = bytearray(a.pagesize)
hdr[0:8] = b'ANDROID!'
struct.pack_into('<10I', hdr, 8,
	len(kernel), a.base + a.kernel_offset,
	0, a.base + a.ramdisk_offset,
	0, a.base + a.second_offset,
	a.base + a.tags_offset, a.pagesize,
	1, os_version)
board = a.board.encode()
assert len(board) < 16, 'board name too long'
hdr[48:48 + len(board)] = board
hdr[576:576 + 20] = sha.digest()
struct.pack_into('<IQI', hdr, 1632, 0, 0, 1648)	# recovery_dtbo size/offset, header_size

pad = (-len(kernel)) % a.pagesize
open(a.out, 'wb').write(bytes(hdr) + kernel + b'\0' * pad)
print(f'{a.out}: header v1, page {a.pagesize}, kernel {len(kernel)} at {a.base + a.kernel_offset:#x}')
