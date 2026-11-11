#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Writes the core tests' GXA fixtures (src/gba/test/gxa): run once, commit the output."""
import hashlib
import io
import json
import pathlib
import sys
import zipfile

import numpy as np
import soundfile as sf

OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'src/gba/test/gxa')


def tone(freq, secs, rate, channels=1, amp=0.5):
    t = np.arange(int(round(secs * rate))) / rate
    x = (amp * np.sin(2 * np.pi * freq * t)).astype(np.float32)
    return np.stack([x] * channels, axis=1) if channels > 1 else x


def ogg(x, rate):
    b = io.BytesIO()
    sf.write(b, x, rate, format='OGG', subtype='VORBIS')
    return b.getvalue()


def pack(path, manifest, files, deflate=()):
    with zipfile.ZipFile(path, 'w') as z:
        z.writestr(zipfile.ZipInfo('manifest.json'), json.dumps(manifest, indent=1), zipfile.ZIP_STORED)
        for name, data in files.items():
            z.writestr(zipfile.ZipInfo(name), data, zipfile.ZIP_DEFLATED if name in deflate else zipfile.ZIP_STORED)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    files = {'t1.ogg': ogg(tone(440, 1.0, 22050), 22050),
             'loop/t2.ogg': ogg(tone(880, 0.5, 48000, 2), 48000),
             't3.ogg': ogg(tone(1000, 0.25, 32768), 32768)}
    manifest = {'spec': 1, 'title': 'GXA test fixture', 'author': 'GoGBA', 'version': '1', 'license': 'MIT',
                'match': [{'code': 'GXAT', 'title': 'GXA TEST'}],
                'tracks': {'1': {'file': 't1.ogg'}, '2': {'file': 'loop/t2.ogg', 'loop': 12000},
                           '3': {'file': 't3.ogg', 'gain': -6}, '7': {'file': 't7.ogg'}}}
    pack(OUT / 'fixture.gxa', manifest, files)
    pack(OUT / 'bad-deflate.gxa', manifest, files, deflate={'t1.ogg'})
    pack(OUT / 'bad-spec.gxa', dict(manifest, spec=2), files)
    pack(OUT / 'sha1.gxa', dict(manifest, match=[{'sha1': hashlib.sha1(test_rom()).hexdigest()}]), files)
    pack(OUT / 'version.gxa', dict(manifest, match=[{'code': 'GXAT', 'title': 'GXA TEST', 'version': [2, 5]},
                                                   {'code': 'GXAV', 'title': 'GXA TEST', 'version': 3}]), files)


def test_rom(code=b'GXAT'):
    """The tests' 192-byte ROM image: zeros, title at 0xA0, game code at 0xAC (src/gba/test/gxa-pack.c)"""
    rom = bytearray(192)
    rom[0xA0:0xA8] = b'GXA TEST'
    rom[0xAC:0xB0] = code
    return bytes(rom)


if __name__ == '__main__':
    main()
