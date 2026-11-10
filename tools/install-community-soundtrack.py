#!/usr/bin/env python3
"""Install the optional restored soundtrack on a closed Quest development app.

Prepare/stage the pack first. Original game archives and careers are untouched.
"""
import argparse
import importlib.util
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = 'org.nfsu2.quest'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--adb', default='adb')
    parser.add_argument('--source', type=Path, default=ROOT / 'build/optional-mod-assets/SONGS')
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('music_stage', ROOT / 'tools/stage-community-soundtrack.py')
    stage = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(stage)
    for name, expected in stage.HASHES.items():
        if not (args.source / name).is_file() or stage.digest(args.source / name) != expected:
            parser.error(f'Prepare the verified soundtrack first: {name}')
    base = [args.adb, '-s', args.serial]
    def adb(*command, check=True):
        return subprocess.run([*base, *command], text=True, capture_output=True, check=check)
    if adb('shell', 'pidof', PACKAGE, check=False).stdout.strip():
        parser.error('Close NFSU2 Development before updating optional packs')
    destination = f'/sdcard/Android/data/{PACKAGE}/files/SONGS'
    adb('shell', f'run-as {PACKAGE} mkdir -p ' + shlex.quote(destination))
    for name, expected in stage.HASHES.items():
        staging = '/data/local/tmp/nfsu2-restored-music.tmp'
        adb('push', str((args.source / name).resolve()), staging)
        temporary, final = destination + '/' + name + '.new', destination + '/' + name
        adb('shell', f'run-as {PACKAGE} cp {shlex.quote(staging)} {shlex.quote(temporary)}')
        actual = adb('shell', f'run-as {PACKAGE} sha256sum {shlex.quote(temporary)}').stdout.split()[0]
        if actual != expected:
            raise RuntimeError(f'Device checksum differs: {name}; previous file preserved')
        adb('shell', f'run-as {PACKAGE} mv {shlex.quote(temporary)} {shlex.quote(final)}')
        adb('shell', 'rm -f ' + shlex.quote(staging))
        print(f'Installed and verified {name}', flush=True)
    print('Enable RESTORED SOUNDTRACK in Community Mods, select Original under Radio Soundtrack, then restart.')


if __name__ == '__main__':
    main()
