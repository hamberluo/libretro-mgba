# SPDX-License-Identifier: MIT
"""gxa_pack.py builds store-only packs the core reads; every listed problem fails with a message."""
import json
import os
import pathlib
import subprocess
import sys
import zipfile

import numpy as np
import soundfile as sf

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
PACK = [sys.executable, str(HERE / 'gxa_pack.py')]
INFO = [sys.executable, str(HERE / 'gxa_info.py')]


def tone(freq, secs, rate, channels=1):
    t = np.arange(int(secs * rate)) / rate
    x = (0.5 * np.sin(2 * np.pi * freq * t)).astype(np.float32)
    return np.stack([x] * channels, axis=1) if channels > 1 else x


def good_dir(d):
    """Mirrors src/gba/test/gxa/fixture.gxa (without its missing track 7), so the core's gxa-pack test reads it."""
    sf.write(d / 't1.wav', tone(440, 1.0, 22050), 22050)
    sf.write(d / 't2.ogg', tone(880, 0.5, 48000, 2), 48000, format='OGG', subtype='VORBIS')
    sf.write(d / 't3.flac', tone(1000, 0.25, 32768), 32768)
    manifest = {'spec': 1, 'title': 'GXA test fixture', 'author': 'GoGBA', 'version': '1', 'license': 'MIT',
                'match': [{'code': 'GXAT', 'title': 'GXA TEST'}],
                'tracks': {'1': {'file': 't1.wav'}, '2': {'file': 't2.ogg', 'loop': 12000},
                           '3': {'file': 't3.flac', 'gain': -6}}}
    (d / 'manifest.json').write_text(json.dumps(manifest))
    return manifest


def test_packs_store_only(tmp_path):
    good_dir(tmp_path)
    out = tmp_path / 'out.gxa'
    r = subprocess.run(PACK + [str(tmp_path), str(out)], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    z = zipfile.ZipFile(out)
    assert all(i.compress_type == zipfile.ZIP_STORED for i in z.infolist())
    m = json.loads(z.read('manifest.json'))
    assert m['tracks']['1']['file'].endswith('.ogg') and m['tracks']['3']['file'].endswith('.ogg')
    assert m['tracks']['2']['loop'] == 12000
    for t in m['tracks'].values():
        assert z.read(t['file'])[:4] == b'OggS'


def test_core_reads_it(tmp_path):
    good_dir(tmp_path)
    out = tmp_path / 'out.gxa'
    subprocess.run(PACK + [str(tmp_path), str(out)], check=True, capture_output=True)
    r = subprocess.run([str(ROOT / 'libretro-build/run_tests.sh'), 'gxa-pack'], capture_output=True, text=True,
                       env=dict(os.environ, GXA_PACK=str(out)))
    assert r.returncode == 0 and 'gxa-pack: ok' in r.stdout, r.stdout[-2000:]


def test_info(tmp_path):
    good_dir(tmp_path)
    out = tmp_path / 'out.gxa'
    subprocess.run(PACK + [str(tmp_path), str(out)], check=True, capture_output=True)
    r = subprocess.run(INFO + [str(out)], capture_output=True, text=True)
    assert r.returncode == 0
    assert 'GXA test fixture' in r.stdout and 'tracks: 3' in r.stdout and 'GXAT' in r.stdout


def broken(tmp_path, change, files=None):
    m = good_dir(tmp_path)
    change(m)
    (tmp_path / 'manifest.json').write_text(json.dumps(m))
    for name, data in (files or {}).items():
        (tmp_path / name).write_bytes(data)
    return subprocess.run(PACK + [str(tmp_path), str(tmp_path / 'out.gxa')], capture_output=True, text=True)


def test_rejects_spec(tmp_path):
    r = broken(tmp_path, lambda m: m.update(spec=2))
    assert r.returncode != 0 and 'spec' in r.stderr


def test_rejects_track_id(tmp_path):
    r = broken(tmp_path, lambda m: m['tracks'].update({'70000': {'file': 't1.wav'}}))
    assert r.returncode != 0 and 'track 70000' in r.stderr


def test_rejects_missing_file(tmp_path):
    r = broken(tmp_path, lambda m: m['tracks'].update({'9': {'file': 'nope.wav'}}))
    assert r.returncode != 0 and 'track 9' in r.stderr


def test_rejects_undecodable(tmp_path):
    r = broken(tmp_path, lambda m: m['tracks'].update({'5': {'file': 'bad.ogg'}}), {'bad.ogg': b'OggS not really'})
    assert r.returncode != 0 and 'track 5' in r.stderr


def test_rejects_loop_past_end(tmp_path):
    r = broken(tmp_path, lambda m: m['tracks']['2'].update(loop=24000))
    assert r.returncode != 0 and 'track 2' in r.stderr


def test_rejects_empty_match(tmp_path):
    r = broken(tmp_path, lambda m: m.update(match=[]))
    assert r.returncode != 0 and 'match' in r.stderr


def test_rejects_bad_version(tmp_path):
    r = broken(tmp_path, lambda m: m.update(match=[{'code': 'GXAT', 'title': 'GXA TEST', 'version': [5, 2]}]))
    assert r.returncode != 0 and 'version' in r.stderr
