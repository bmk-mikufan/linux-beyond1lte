#!/usr/bin/env python3
"""An empty AVB vbmeta image with verification disabled (what `avbtool make_vbmeta_image
--flags 2` writes): no key, no signature, no descriptors.

The stock SM-G973N vbmeta verifies the boot partition by a hash descriptor, so a boot.img
holding U-Boot fails it. With the bootloader unlocked, a vbmeta whose flags say
VERIFICATION_DISABLED (2) is accepted, as for TWRP and Magisk on the S10. No Samsung data
is in it. Going back to stock firmware writes Samsung's vbmeta again.

  make-vbmeta-disabled.py OUT
"""
import struct
import sys

FLAGS = 2	# AVB_VBMETA_IMAGE_FLAGS_VERIFICATION_DISABLED
hdr = struct.pack('>4sII2QI2Q2Q2Q2Q2QQII48s',
	b'AVB0', 1, 0,		# magic, required libavb 1.0
	0, 0,			# authentication / auxiliary block sizes
	0,			# algorithm NONE
	0, 0, 0, 0,		# hash, signature
	0, 0, 0, 0,		# public key, its metadata
	0, 0,			# descriptors
	0, FLAGS, 0,		# rollback index, flags, rollback index location
	b'avbtool 1.2.0')	# release string, as avbtool writes it
hdr += b'\0' * (256 - len(hdr))
assert len(hdr) == 256
open(sys.argv[1], 'wb').write(hdr + b'\0' * (4096 - 256))	# avbtool pads to 4 KiB
print(f'{sys.argv[1]}: vbmeta, flags {FLAGS} (verification disabled)')
