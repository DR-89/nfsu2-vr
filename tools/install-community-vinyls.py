#!/usr/bin/env python3
"""Install optional HQ vinyl packs into the Quest app's external files folder.

The app must be closed. This does not install an APK, alter game archives or
touch saved careers. Run after preparing the packs on the development PC.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = 'org.nfsu2.quest'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--adb', default='adb')
    parser.add_argument('--source', type=Path, default=ROOT / 'build/optional-mod-assets/HQVINYLS')
    args = parser.parse_args()
    files = []
    for path in sorted(args.source.glob('*.nhq')):
        if not path.stem.isalnum() or path.stem != path.stem.upper():
            parser.error('Invalid model pack filename')
        expected = json.loads(path.with_suffix('.json').read_text())['sha256']
        with path.open('rb') as f:
            if hashlib.file_digest(f, 'sha256').hexdigest() != expected:
                parser.error(f'Prepared pack checksum differs: {path.name}')
        files.append((path, expected))
    if not files:
        parser.error('Prepare optional vinyl packs first')
    base = [args.adb, '-s', args.serial]
    def adb(*command, check=True):
        return subprocess.run([*base, *command], text=True, capture_output=True, check=check)
    if adb('shell', 'pidof', PACKAGE, check=False).stdout.strip():
        parser.error('Close NFSU2 Development before updating its optional packs')
    destination = f'/sdcard/Android/data/{PACKAGE}/files/HQVINYLS'
    staging = '/data/local/tmp/nfsu2-vinyl-pack.nhq'
    adb('shell', f'run-as {PACKAGE} mkdir -p ' + shlex.quote(destination))
    for path, expected in files:
        adb('push', str(path.resolve()), staging)
        temporary = destination + '/' + path.name + '.new'
        final = destination + '/' + path.name
        adb('shell', f'run-as {PACKAGE} cp {shlex.quote(staging)} {shlex.quote(temporary)}')
        actual = adb('shell', f'run-as {PACKAGE} sha256sum {shlex.quote(temporary)}').stdout.split()[0]
        if actual != expected:
            raise RuntimeError(f'Device checksum differs for {path.name}; previous pack preserved')
        adb('shell', f'run-as {PACKAGE} mv {shlex.quote(temporary)} {shlex.quote(final)}')
        print(f'Installed and verified {path.name}', flush=True)
    print('Enable HQ VINYLS / DOORLINES in VR Settings > Community Mods, then restart.')


if __name__ == '__main__':
    main()
