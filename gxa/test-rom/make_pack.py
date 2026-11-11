#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Writes the conformance ROM's pack sources (tones and manifest) into a directory for gxa_pack.py."""
import json
import pathlib
import sys

import numpy as np
import soundfile as sf

d = pathlib.Path(sys.argv[1])
d.mkdir(parents=True, exist_ok=True)
for name, freq, secs in (('one-second', 440, 1.0), ('loop', 660, 0.5)):
    t = np.arange(int(secs * 22050)) / 22050
    sf.write(d / (name + '.wav'), (0.4 * np.sin(2 * np.pi * freq * t)).astype(np.float32), 22050)
(d / 'manifest.json').write_text(json.dumps({
    'spec': 1, 'title': 'GXA conformance', 'author': 'GoGBA', 'version': '1', 'license': 'MIT',
    'match': [{'code': 'GXAC', 'title': 'GXA CONFORM'}],
    'tracks': {'1': {'file': 'one-second.wav'}, '2': {'file': 'loop.wav', 'loop': 0}}}, indent=1))
