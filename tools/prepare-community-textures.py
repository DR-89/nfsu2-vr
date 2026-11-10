#!/usr/bin/env python3
"""Stage the pinned optional texture packs from an existing local xyz export.

Obtain TEXPACKS with the site's normal game-data export/read API, or build
them using th0mk/nfsu2-web tools/texpacks.py and the credited authors' mods.
No game archives or artwork belong in the source repository. A clean build
without these optional packs remains usable with the original textures.
"""
import argparse
import hashlib
import os
from pathlib import Path
import tempfile
import shutil

ROOT = Path(__file__).resolve().parents[1]
FILES = {
    'uhud.pak': '2a0eb5adb888f54d341dd429ca089280cacca0a03fdc5191b286133db0ba7df7',
    'detailed.pak': '87af5388b3fd0ddc9a91a37986157e0cf0669166fadbc8169c326c095d9e8ea5',
    'detailed.tracks': '1b38f723b68661164b7c4db429d082f6f40898e6bc812863eb5cd8ded790e57f',
}

def valid(path, digest):
    if not path.is_file():
        return False
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest() == digest

def copy_atomic(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as output:
        temporary = Path(output.name)
        with source.open('rb') as input_file:
            shutil.copyfileobj(input_file, output)
    try:
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'build/xyz-lan-qa/live-mod-assets')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/mod-assets')
    args = parser.parse_args()
    destination = args.output.resolve() / 'TEXPACKS'
    if destination.exists():
        copy_atomic(ROOT / 'src/community/vendor/texpacks/ARTWORK-NOTICE.txt', destination / 'NOTICE.txt')
    subtitles = ROOT / 'build/mod-assets/TEXPACKS/english-subtitles.bin'
    if subtitles.is_file() and subtitles.resolve() != (destination/'english-subtitles.bin').resolve():
        if not valid(subtitles, 'f5059af6170fec556eac28c24d211a70896485cd9d98cc006a4b8f9b6dfd1dab'):
            raise SystemExit('Unexpected prepared subtitle pack')
        destination.mkdir(parents=True, exist_ok=True)
        copy_atomic(subtitles,destination/'english-subtitles.bin')
    if all(valid(destination / name, sha) for name, sha in FILES.items()):
        print('Community textures: pinned packs already staged')
        return
    source = args.source.resolve() / 'TEXPACKS'
    if not source.is_dir():
        print('Optional community textures unavailable; original textures remain available')
        return
    if not all(valid(source / name, sha) for name, sha in FILES.items()):
        raise SystemExit('Unexpected texture asset hashes; no destination files changed')
    destination.mkdir(parents=True, exist_ok=True)
    for name, sha in FILES.items():
        if valid(destination / name, sha):
            continue
        with tempfile.NamedTemporaryFile(dir=destination, delete=False) as output:
            temporary = Path(output.name)
            with (source / name).open('rb') as input_file:
                while block := input_file.read(1024 * 1024):
                    output.write(block)
        try:
            os.replace(temporary, destination / name)
        finally:
            temporary.unlink(missing_ok=True)
    (destination / 'NOTICE.txt').write_text((ROOT / 'src/community/vendor/texpacks/ARTWORK-NOTICE.txt').read_text(), encoding='utf-8')
    print('Community textures: Upscaled HUD, Detailed Map and calibration staged')

if __name__ == '__main__':
    main()
