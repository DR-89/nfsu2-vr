#!/usr/bin/env python3
"""Prepare and stage the restored soundtrack's verified Xbox archive overlay.

Run prepare-community-soundtrack.py first. Only optional output files change;
the owned disc, careers, and the original EA playlist remain intact.
"""
import argparse
import hashlib
import importlib.util
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
HASHES = {
    'MusicSFX.mus': '194745211c1405ae53410cc0b1bd5a2542897d05df4d1ed1db58b2aac79d3f89',
    'MusicSFX.mpf': 'cb3d11a25c5ac1c752aad68bfc045ec72b1df0bb05ef91685d90fae9948b592d',
    'MusicSFX.sbk': '40261ad0ccab2864e04a98a8118496034206b83567d3c2af7ab202482d6b2bf8',
    'ZDIR.BIN': 'd97ca4681691163425a0033764eaa5e0c43e1d2355736c8ba5f3e6d7cbf2bef2',
}
NOTICE = '''Uncensored & Restored Soundtrack by xGMVx
https://nfsmods.xyz/mod/3649
Source pack: nfsu2.xyz / th0mk/nfsu2-web tools/musicpack.py.
This locally prepared Xbox pack preserves decoded signed 16-bit samples in
EA-XA v2 PCM escape frames. The original game controls playback and EA TRAX.
Music copyright remains with its respective rights holders. These files are
optional local assets, not part of the port's source-code licence.
'''


def digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'build/optional-mod-assets/SONGS')
    parser.add_argument('--game', type=Path, default=ROOT / 'game')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/pcvr/game')
    args = parser.parse_args()
    if not (args.source / 'MusicSFX.mus').is_file():
        print('Optional restored soundtrack unavailable; original music remains available')
        return
    for name in ('MusicSFX.mpf', 'MusicSFX.mus'):
        if digest(args.source / name) != HASHES[name]:
            raise SystemExit(f'Prepared soundtrack checksum differs: {name}; nothing staged')
    zdir = bytearray((args.game / 'NFSUNDER/ZDIR.BIN').read_bytes())
    if hashlib.sha256(zdir).hexdigest() != '254212625c80eb720daf139290b8d19aa159cb6890bebc75d11c2d3da52fb722':
        raise SystemExit('Unsupported original Xbox directory')
    # The game normally reads its MPF embedded in this sound bank. Both
    # duplicate SBK entries must point at the same patched bank; replacing
    # only the separately archived MPF leaves stale song offsets in use.
    with (args.game / 'NFSUNDER/ZZDATA2.BIN').open('rb') as f:
        f.seek(15794 * 2048)
        sbk = bytearray(f.read(38932))
    if hashlib.sha256(sbk).hexdigest() != '9438f54a8832dbcaf82e6c13d53078ee47aa92d54f4ec95e4feba4adc0ae6bb2':
        raise SystemExit('Unsupported original Xbox sound bank')
    sbk[31088:37928] = (args.source / 'MusicSFX.mpf').read_bytes()
    for i in range(0, len(zdir), 24):
        key = struct.unpack_from('<I', zdir, i)[0]
        if key == 1628346117:
            struct.pack_into('<IIII', zdir, i + 4, 10, 0, 850398, 1626612096)
        elif key == 2228279645:
            struct.pack_into('<II', zdir, i + 4, 11, 0)
        elif key == 1628345939:
            struct.pack_into('<II', zdir, i + 4, 12, 0)
    for name, data in (('ZDIR.BIN', zdir), ('MusicSFX.sbk', sbk)):
        if hashlib.sha256(data).hexdigest() != HASHES[name]:
            raise SystemExit(f'Overlay verification failed: {name}')
        (args.source / name).write_bytes(data)
    spec = importlib.util.spec_from_file_location('vinyl_stage', ROOT / 'tools/stage-community-vinyls.py')
    stage = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(stage)
    for name in HASHES:
        stage.stage(args.source / name, args.output / 'SONGS' / name)
    (args.output / 'SONGS/NOTICE.txt').write_text(NOTICE, encoding='utf-8')
    print('Restored soundtrack: verified music, directory and both embedded bank references staged')


if __name__ == '__main__':
    main()
