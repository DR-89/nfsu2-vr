#!/usr/bin/env python3
"""Stage verified, already converted HQ vinyl packs next to a PCVR executable.

Uses hard links on the same volume, otherwise copies. Individual packs are
replaced atomically. The large optional packs are not embedded in the APK.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def stage(source, destination):
    if source.resolve() == destination.resolve():
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as f:
        temporary = Path(f.name)
    temporary.unlink()
    try:
        try:
            os.link(source, temporary)
        except OSError:
            shutil.copyfile(source, temporary)
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'build/optional-mod-assets/HQVINYLS')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/pcvr/game')
    args = parser.parse_args()
    sources = []
    for path in sorted(args.source.glob('*.nhq')):
        metadata = json.loads(path.with_suffix('.json').read_text())
        if metadata.get('model') != path.stem or metadata.get('format') != 'NHQ1' or sha(path) != metadata.get('sha256'):
            raise SystemExit(f'Invalid prepared pack: {path.name}; nothing staged')
        sources.append((path, metadata))
    if not sources:
        print('Optional HQ vinyl packs unavailable; original skins remain available')
        return
    destination = args.output / 'HQVINYLS'
    for path, metadata in sources:
        stage(path, destination / path.name)
        stage(path.with_suffix('.json'), destination / path.with_suffix('.json').name)
    (destination / 'NOTICE.txt').write_text((ROOT / 'src/community/vendor/texpacks/ARTWORK-NOTICE.txt').read_text(), encoding='utf-8')
    print(f'HQ vinyls: {len(sources)} verified model packs staged')


if __name__ == '__main__':
    main()
