#!/bin/sh
# SPDX-License-Identifier: MIT
# Builds gxa-conformance.gba and its pack gxa-conformance.gxa here (both committed).
# PY: a python with soundfile and numpy (gxa/tools/requirements.txt).
set -e
cd "$(dirname "$0")"
PY=${PY:-python3}
T=$(mktemp -d)
arm-none-eabi-gcc -mcpu=arm7tdmi -mthumb -mthumb-interwork -O2 -ffreestanding -nostdlib -Wall \
	-T gba.ld -o "$T/rom.elf" crt0.s main.c -lgcc
arm-none-eabi-objcopy -O binary "$T/rom.elf" gxa-conformance.gba
"$PY" - gxa-conformance.gba <<'PYEOF'
import sys, zlib
LOGO = bytes.fromhex(
    '24FFAE51699AA2213D84820A84E409AD11248B98C0817F21A352BE199309CE2010464A4AF82731EC58C7E83382E3CEBF85F4DF94CE4B'
    '09C194568AC01372A7FC9F844D73A3CA9A615897A327FC039876231DC7610304AE56BF38840040A70EFDFF52FE036F9530F197FBC085'
    '60D68025A963BE03014E38E2F9A234FFBB3E0344780090CB88113A9465C07C6387F03CAFD625E48B380AAC7221D4F807')
assert zlib.crc32(LOGO) == 0xD0BEB55E
p = sys.argv[1]
d = bytearray(open(p, 'rb').read())
d[4:160] = LOGO
d[0xBD] = (-(sum(d[0xA0:0xBD]) + 0x19)) & 0xFF
open(p, 'wb').write(d)
PYEOF
"$PY" make_pack.py "$T/pack"
"$PY" ../tools/gxa_pack.py "$T/pack" gxa-conformance.gxa
rm -rf "$T"
ls -l gxa-conformance.gba gxa-conformance.gxa
