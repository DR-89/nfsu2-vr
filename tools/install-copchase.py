#!/usr/bin/env python3
"""Verify and stage the user's prepared CopChase pack for native PCVR or Quest.

No PC ASI, retail disc, settings or saved careers are installed or modified.
Quest staging is offline: copy the resulting COPCHASE folder to the development
app's external files directory with the app closed. This tool never contacts ADB.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ZIP_SHA = 'f7d68dc92dd2cb78d7dcc3e09e1e17b1444156cd8d674c0e3c72997e5d984f8d'
MIXED = ['4DR_SEDAN', 'MINIVAN', 'COUPE', 'TAXI02', 'HATCHBACK', 'BUS', 'PICKUP', 'SUV']
PINS = {
    'ZZDATA13.BIN': (9512960, 'dae44d4eeecb2f505e8f2d3b52eb7ced31cd005df8375bdf996b7b9af0cca794'),
    'ZDIR.BIN': (32688, '43dd661b62e6b35e35ad99beb5389cd8eec9b9ea8cf82fa3c071318bd4d5c29c'),
    'ZDIR-SONGS.BIN': (32688, '6aa6b461c0645a58e062ac7fd1bcfdb78f2339bab6381355d12d32952baafccd'),
    'resources/audio/native-manifest.json': (28190, '92a9e74bf8a23411462f0eddf43ab36d300600caf818084a9acc1513f7a85121'),
}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def relative(name):
    require(isinstance(name, str) and name and '\\' not in name and ':' not in name and '\0' not in name,
            'Invalid asset path')
    parts = name.split('/')
    require(not PurePosixPath(name).is_absolute() and all(p and p not in ('.', '..') and
            not p.endswith((' ', '.')) for p in parts), 'Unsafe asset path')
    return Path(*parts)


def unlinked(path):
    """Reject symlinks and Windows junctions before resolving containment."""
    path = path.absolute()
    for candidate in (path, *path.parents):
        try:
            info = candidate.lstat()
        except FileNotFoundError:
            continue
        require(not stat.S_ISLNK(info.st_mode) and not (getattr(info, 'st_file_attributes', 0) & 0x400),
                f'Linked/reparse path is not a staging destination: {candidate}')
    return path


def bounded(root, name):
    path = root / relative(name)
    unlinked(path)
    require(path.resolve().is_relative_to(root.resolve()), 'Asset path escapes its directory')
    return path


def runtime_name(name):
    return name in ('ZZDATA13.BIN', 'ZDIR.BIN', 'ZDIR-SONGS.BIN', 'NOTICE.txt') or \
        name.startswith('resources/') or name.startswith('provenance/')


def verified(source):
    source = unlinked(source).resolve()
    manifest_path = bounded(source, 'manifest.json')
    require(manifest_path.stat().st_size <= 2 * 1024 * 1024, 'Oversized preparation manifest')
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    require(manifest.get('format') == 'NCPCH1' and manifest.get('version') == '0.3.2' and
            manifest.get('density') == 'mixed' and manifest.get('active_slots') == MIXED,
            'Requires the verified mixed8 v0.3.2 native preparation')
    require(manifest.get('source_archive', {}).get('sha256') == ZIP_SHA, 'Unsupported source provenance')
    specs = manifest.get('files', {})
    require(isinstance(specs, dict) and 1000 < len(specs) < 1200, 'Incomplete preparation manifest')
    for name, (size, checksum) in PINS.items():
        require(specs.get(name, {}).get('bytes') == size and specs[name].get('sha256') == checksum,
                f'Unsupported native pack revision: {name}')
    files = []
    for name, spec in sorted(specs.items()):
        relative(name)  # Reject unsafe manifest entries even when not deployed.
        if not runtime_name(name):
            continue
        require(isinstance(spec, dict) and isinstance(spec.get('bytes'), int) and 0 <= spec['bytes'] <= 64*1024*1024 and
                isinstance(spec.get('sha256'), str) and len(spec['sha256']) == 64, 'Invalid file metadata')
        path = bounded(source, name)
        require(path.is_file() and path.stat().st_size == spec['bytes'] and digest(path) == spec['sha256'],
                f'Prepared asset missing or changed: {name}; nothing staged')
        files.append((path, name, spec['sha256']))
    names = {name for _, name, _ in files}
    require({'NOTICE.txt', 'provenance/Terms of Use.txt', 'provenance/README.txt',
             'resources/audio/chatter_tags.txt', 'resources/audio/manifest.json',
             'resources/roadmap.bin', 'resources/sightmap.bin'}.issubset(names), 'Missing provenance/resources')
    require(sum(name.endswith('.wav') for name in names) == 1063, 'Incomplete original audio resources')
    files.append((manifest_path, 'manifest.json', digest(manifest_path)))
    return files


def destination(parent, source, pcvr):
    parent = unlinked(parent).resolve()
    require(not any((parent / name).exists() for name in ('SPEED2.EXE', 'speed2.exe', 'default.xbe', 'NFSUNDER')),
            'Choose the native executable directory, not an original game/disc folder')
    if pcvr:
        require((parent / 'nfsu2-pcvr.exe').is_file() or (parent / 'nfsu2-pcvr-startup.exe').is_file(),
                'PCVR destination must contain the native executable')
    else:
        require(parent.is_relative_to((ROOT / 'build').resolve()), 'Offline Quest staging must stay below this workspace build directory')
    target = unlinked(parent / 'COPCHASE')
    source = source.resolve()
    require(not target.resolve().is_relative_to(source) and not source.is_relative_to(target.resolve()),
            'Source and destination cannot overlap')
    return target


def stage(source, dest, expected):
    if dest.is_file() and digest(dest) == expected:
        return False
    dest.parent.mkdir(parents=True, exist_ok=True)
    unlinked(dest)
    descriptor, name = tempfile.mkstemp(prefix='.copchase-', suffix='.new', dir=dest.parent)
    os.close(descriptor)
    temp = Path(name)
    try:
        shutil.copyfile(source, temp)
        require(digest(temp) == expected, f'Copy verification failed: {dest.name}')
        os.replace(temp, dest)
    finally:
        temp.unlink(missing_ok=True)
    return True


def install(source, parent, pcvr=False, verify_only=False):
    files = verified(source)
    target = destination(parent, source, pcvr)
    # Validate the entire output path set before any write. Existing caches,
    # diagnostic fixtures and unrelated files remain outside the copy set.
    outputs = [(src, bounded(target, name), name, checksum) for src, name, checksum in files]
    if verify_only:
        missing = [name for _, dest, name, checksum in outputs if not dest.is_file() or digest(dest) != checksum]
        return {'destination': str(target), 'files': len(files), 'different': missing, 'written': 0}
    written = sum(stage(src, dest, checksum) for src, dest, _, checksum in outputs)
    return {'destination': str(target), 'files': len(files), 'bytes': sum(src.stat().st_size for src, _, _ in files),
            'written': written, 'credit': 'CopChase by Vitorini Mods v0.3.2',
            'source_archive_sha256': ZIP_SHA, 'settings_changed': False, 'original_game_changed': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'build/mod-assets/copchase')
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument('--pcvr', type=Path, help='Native executable directory; close the app before changing data')
    target.add_argument('--quest-staging', type=Path, help='Offline directory under build; no device connection')
    parser.add_argument('--verify-only', action='store_true', help='Report differences without writing')
    args = parser.parse_args()
    try:
        report = install(args.source, args.pcvr or args.quest_staging, bool(args.pcvr), args.verify_only)
        print(json.dumps(report, indent=2))
        if not args.verify_only:
            print('Private data staged. CopChase remains off until enabled in Community Mods, saved and restarted.')
            if args.quest_staging:
                print('Quest destination: /sdcard/Android/data/org.nfsu2.quest/files/COPCHASE (app closed). No device was contacted.')
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f'CopChase staging failed: {error}\n')


if __name__ == '__main__':
    main()
