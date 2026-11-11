#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Builds a GXA pack (gxa/SPEC.md): a store-only zip of manifest.json and Ogg Vorbis tracks.

usage: gxa_pack.py <dir> <out.gxa> [--quality Q]
<dir> holds manifest.json and the files its tracks name. WAV and FLAC files are encoded to Ogg Vorbis
(quality 0-1, default 0.4); Ogg files are copied as they are."""
import argparse
import io
import json
import pathlib
import sys
import zipfile

import soundfile as sf


def encode(path, quality):
    x, rate = sf.read(str(path), dtype='float32')
    b = io.BytesIO()
    # libsndfile's Vorbis compression level is 1 - quality
    sf.write(b, x, rate, format='OGG', subtype='VORBIS', compression_level=1 - quality)
    return b.getvalue()


def frames(data):
    info = sf.info(io.BytesIO(data))
    if info.format != 'OGG' or info.subtype != 'VORBIS':
        raise ValueError('not Ogg Vorbis')
    return info.frames


def build(src, out, quality=0.4):
    """Returns the list of problems; writes out only when there are none."""
    src = pathlib.Path(src)
    m = json.loads((src / 'manifest.json').read_text())
    errors = []
    if m.get('spec') != 1:
        errors.append('spec must be 1')
    if not m.get('match'):
        errors.append('match is empty: no ROM would ever load this pack')
    for rule in m.get('match', []):
        v = rule.get('version')
        lo, hi = (v, 255) if isinstance(v, int) else (v if isinstance(v, list) and len(v) == 2 else (-1, -1))
        if 'version' in rule and not (isinstance(lo, int) and isinstance(hi, int) and 0 <= lo <= hi <= 255):
            errors.append('match version must be 0-255 or [min, max] within it: %r' % (v,))
    entries = {}
    tracks = {}
    for key, t in m.get('tracks', {}).items():
        where = 'track %s' % key
        if not key.isdigit() or int(key) > 0xFFFF:
            errors.append('%s: ids are 0-65535' % where)
            continue
        path = src / t.get('file', '')
        if not t.get('file') or not path.is_file():
            errors.append('%s: no file %s' % (where, t.get('file')))
            continue
        try:
            data = path.read_bytes() if path.suffix.lower() == '.ogg' else encode(path, quality)
            n = frames(data)
        except Exception as e:  # noqa: BLE001 - any decoder error is the same problem
            errors.append('%s: cannot decode %s (%s)' % (where, t['file'], e))
            continue
        if 'loop' in t and not 0 <= t['loop'] < n:
            errors.append('%s: loop %s is not inside its %d frames' % (where, t['loop'], n))
            continue
        name = str(pathlib.PurePosixPath(t['file']).with_suffix('.ogg'))
        entries[name] = data
        tracks[key] = dict(t, file=name)
    if errors:
        return errors
    m = dict(m, tracks=tracks)
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_STORED) as z:
        z.writestr('manifest.json', json.dumps(m, indent=1, ensure_ascii=False))
        for name, data in entries.items():
            z.writestr(name, data)
    return []


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('dir')
    ap.add_argument('out')
    ap.add_argument('--quality', type=float, default=0.4)
    a = ap.parse_args()
    errors = build(a.dir, a.out, a.quality)
    for e in errors:
        print(e, file=sys.stderr)
    sys.exit(1 if errors else 0)


if __name__ == '__main__':
    main()
