#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Prints what a GXA pack holds: usage: gxa_info.py <pack.gxa>"""
import io
import json
import os
import sys
import zipfile

import soundfile as sf


def main():
    path = sys.argv[1]
    z = zipfile.ZipFile(path)
    m = json.loads(z.read('manifest.json'))
    print('title: %s' % m.get('title', ''))
    print('author: %s' % m.get('author', ''))
    print('version: %s' % m.get('version', ''))
    print('license: %s' % m.get('license', ''))
    for rule in m.get('match', []):
        print('match: %s' % ('sha1 ' + rule['sha1'] if 'sha1' in rule else '%s "%s"%s' % (rule['code'], rule['title'], ' version %s' % (rule['version'],) if 'version' in rule else '')))
    secs = 0.0
    for t in m.get('tracks', {}).values():
        info = sf.info(io.BytesIO(z.read(t['file'])))
        secs += info.frames / info.samplerate
    print('tracks: %d' % len(m.get('tracks', {})))
    print('duration: %d:%02d' % (secs // 60, secs % 60))
    print('size: %.1f MB' % (os.path.getsize(path) / 1e6))


if __name__ == '__main__':
    main()
