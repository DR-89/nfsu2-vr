#!/usr/bin/env python3
"""Stage the three pinned UG2WhineGen sound files; no plugin binaries.

The optional native Transmission Whine uses ropi's public 1.0.2 release.
Generated/downloaded assets stay under build/ and are never source files.
"""
import argparse
import hashlib
import io
import os
from pathlib import Path
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
URL = 'https://github.com/ropi336/UG2WhineGen/releases/download/1.0.2/ug2WhineGen-1-0-2.zip'
ARCHIVE = '112c98ca9a29ea39e592edfb668d7caaf9f5118d7d634638c1b747635d3da2ef'
SOUNDS = {
    'car_whine.wav': 'c03885fa49022e18973767d1140afd9ef8a2c447c0096aa576c4e94390e9b5ab',
    'car_whine_2.wav': 'a85d8782a92ed1aa0b0a9caac90a98e8c387b1d6776b75d9059868197e7cb7c3',
    'car_whine_reverse.wav': '4fe209772b86aeb45410fd93f974ca43b3d635ea4125d1c0758218795e05dc93',
}

def atomic_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as f:
        temporary = Path(f.name)
        f.write(data)
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/mod-assets')
    parser.add_argument('--archive', type=Path, default=ROOT / 'build/deps/ug2WhineGen-1-0-2.zip')
    args = parser.parse_args()
    dest = args.output.resolve() / 'MODPACKS/WHINE'
    if all((dest / n).is_file() and hashlib.sha256((dest / n).read_bytes()).hexdigest() == sha for n, sha in SOUNDS.items()):
        print('Transmission Whine: three pinned sounds already prepared')
        return
    if args.archive.is_file():
        data = args.archive.read_bytes()
    else:
        with urllib.request.urlopen(URL, timeout=45) as response:
            data = response.read(2 * 1024 * 1024)
    if hashlib.sha256(data).hexdigest() != ARCHIVE:
        raise SystemExit('Unexpected UG2WhineGen archive; no sound files changed')
    sounds = {}
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        for entry in archive.infolist():
            parts = entry.filename.replace('\\', '/').lower().split('/')
            if len(parts) >= 2 and parts[-2] == 'snd' and parts[-1] in SOUNDS:
                name = parts[-1]
                if name in sounds or entry.file_size > 4 * 1024 * 1024:
                    raise SystemExit('Unexpected sound entry; no sound files changed')
                sounds[name] = archive.read(entry)
    if set(sounds) != set(SOUNDS) or any(hashlib.sha256(sounds[n]).hexdigest() != sha for n, sha in SOUNDS.items()):
        raise SystemExit('Unexpected sound hashes; no sound files changed')
    if not args.archive.is_file():
        atomic_write(args.archive, data)
    for name, sound in sounds.items():
        atomic_write(dest / name, sound)
    print(f'Transmission Whine: three pinned sounds prepared in {dest}')

if __name__ == '__main__':
    main()
