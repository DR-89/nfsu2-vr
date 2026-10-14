#!/usr/bin/env python3
"""Install the private development APK and verify the user's game assets on Quest."""
import argparse
import hashlib
from pathlib import Path
import shlex
import subprocess

from inspect_game import ROOT, inspect

PACKAGE = 'org.nfsu2.quest'
STAGING = '/data/local/tmp/nfsu2-assets'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True, help='Authorized Quest serial from adb devices')
    parser.add_argument('--adb', default='adb')
    parser.add_argument('--game', type=Path, default=ROOT / 'game')
    parser.add_argument('--apk', type=Path, default=ROOT / 'android/app/build/outputs/apk/debug/app-debug.apk')
    parser.add_argument('--start', action='store_true')
    args = parser.parse_args()
    validation = inspect(args.game)
    if validation['kind'] != 'extracted_disc' or not validation['ok']:
        parser.error('A validated NTSC-U extracted disc is required.')
    if not args.apk.is_file():
        parser.error('Build the APK with tools/build-quest.ps1 first.')
    base = [args.adb, '-s', args.serial]

    def adb(*command, capture=False):
        result = subprocess.run([*base, *command], check=True, text=True,
                                stdout=subprocess.PIPE if capture else None)
        return result.stdout if capture else ''

    game = args.game.resolve()
    xbe = next(p for p in game.iterdir() if p.name.lower() == 'default.xbe')
    assets = next(p for p in game.iterdir() if p.name.upper() == 'NFSUNDER')
    files = [xbe, *sorted(p for p in assets.rglob('*') if p.is_file())]
    adb('install', '-r', str(args.apk.resolve()))
    adb('shell', 'mkdir -p ' + shlex.quote(STAGING))
    # adbd cannot reliably chown files in Android/data on current Quest firmware.
    # Its temporary directory works; run-as then copies into the debug app's own
    # private storage. No broad Android storage permission is needed.
    adb('push', '--sync', str(xbe), str(assets), STAGING + '/')
    adb('shell', f'run-as {PACKAGE} mkdir -p files/game')
    adb('shell', f'run-as {PACKAGE} cp -R {STAGING}/. files/game/')
    for path in files:
        relative = path.relative_to(game).as_posix()
        with path.open('rb') as source:
            expected = hashlib.file_digest(source, 'sha256').hexdigest()
        output = adb('shell', f'run-as {PACKAGE} sha256sum ' + shlex.quote('files/game/' + relative), capture=True)
        if output.split()[0] != expected:
            raise RuntimeError(f'Device hash mismatch: {relative}; game will not be started')
        print(f'Verified {relative}', flush=True)
    print(f'All {len(files)} game files verified.', flush=True)
    if args.start:
        adb('shell', 'am', 'force-stop', PACKAGE)
        adb('shell', 'am', 'start', '-n', PACKAGE + '/.QuestActivity')


if __name__ == '__main__':
    main()
