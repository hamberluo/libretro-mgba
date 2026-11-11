#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Checks SPEC.md's register numbers against the reference implementation (src/gba/gxa.c, gxa.h)."""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = (ROOT / 'gxa/SPEC.md').read_text()
code = (ROOT / 'src/gba/gxa.c').read_text() + (ROOT / 'include/mgba/internal/gba/gxa.h').read_text()


def const(name):
    m = re.search(r'\b%s\s*=?\s*(0x[0-9A-Fa-f]+|\d+)' % name, code)
    return int(m.group(1), 0) if m else None


def row(name):
    m = re.search(r'\| `\+?(0x[0-9A-Fa-f]+)` \| `%s` \| [RW]+ \| `?([0-9A-Fa-fx]*)`?' % name, spec)
    return (int(m.group(1), 0), int(m.group(2), 0) if m.group(2) else None) if m else (None, None)


want = {  # SPEC row -> (offset constant, value constant)
    'ID0': ('REG_ID0', 'GXA_ID'), 'ID1': ('REG_ID1', 'GXA_ID1'), 'HANDSHAKE': ('REG_HANDSHAKE', None),
    'VERSION': ('REG_VERSION', 'GXA_VERSION'), 'STATUS': ('REG_STATUS', None), 'GAMEVOL': ('REG_GAMEVOL', 'GXA_UNITY'),
    'DUCK': ('REG_DUCK', 'GXA_UNITY'), 'TRACK': ('CH_TRACK', None), 'CTRL': ('CH_CTRL', None),
    'VOL': ('CH_VOL', 'GXA_UNITY'), 'FADE': ('CH_FADE', None), 'STATE': ('CH_STATE', None), 'POS': ('CH_POS_LO', None)}
bad = []
for name, (off, val) in want.items():
    o, v = row(name)
    if o is None or o != const(off):
        bad.append('%s: offset %s in SPEC.md, %s in code' % (name, o, const(off)))
    if val and v != const(val):
        bad.append('%s: value %s in SPEC.md, %s in code' % (name, v, const(val)))
checks = [('base', '0x04FFFA00', const('GXA_BASE') == 0xFFFA00), ('channels', 'There are 4 channels', const('GXA_CHANNELS') == 4),
          ('handshake xor', 'v ^ 0x5847', const('GXA_ID') == 0x5847)]
for what, text, ok in checks:
    if text not in spec or not ok:
        bad.append('%s: SPEC.md and code disagree' % what)
for i, cmd in enumerate(['play', 'loop', 'stop', 'pause', 'resume'], 1):
    if not re.search(r'\| %d \| %s \|' % (i, cmd), spec) or const('CTRL_' + cmd.upper()) != i:
        bad.append('CTRL %s: SPEC.md and code disagree' % cmd)
for bit, name in enumerate(['PLAYING', 'PAUSED', 'ENDED', 'MISSING', 'ERROR']):
    if const('STATE_' + name) != 1 << bit or 'bit %d %s' % (bit, name.lower()) not in spec.replace('Bit 0 playing', 'bit 0 playing'):
        bad.append('STATE %s: SPEC.md and code disagree' % name)
for b in bad:
    print(b)
print('check_spec: %s' % ('%d mismatches' % len(bad) if bad else 'ok'))
sys.exit(1 if bad else 0)
