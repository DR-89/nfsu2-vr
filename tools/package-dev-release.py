#!/usr/bin/env python3
"""Package audited PCVR/Quest development builds, never game inputs or saves."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, required=True, help='Clean, published source checkout')
    parser.add_argument('--crt-dir', type=Path, required=True, help='Visual Studio x64 Microsoft.VC143.CRT redistributable directory')
    parser.add_argument('--output', type=Path, required=True, help='New output directory')
    parser.add_argument('--version', default='dev-2026-10-14')
    args = parser.parse_args()
    if any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._' for c in args.version):
        parser.error('Version may contain only letters, digits, periods, underscores and hyphens')
    source = args.source_dir.resolve()
    if subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain']).strip():
        parser.error('The source checkout must be clean')
    commit = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD']).decode().strip()
    args.output.mkdir(parents=True, exist_ok=False)
    package = args.output / 'PCVR'
    package.mkdir()
    inputs = {name: ROOT / 'build/pcvr/game' / name for name in ('nfsu2-pcvr.exe', 'SDL2.dll', 'openxr_loader.dll')}
    inputs.update({name: args.crt_dir / name for name in ('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')})
    inputs.update({'Start-PCVR.cmd': ROOT / 'pcvr/release/Start-PCVR.cmd',
                   'README.txt': ROOT / 'pcvr/release/README.txt',
                   'pcvr.env': ROOT / 'pcvr/benchmark.env',
                   'LICENSES/runtime-third-party-notices.txt': ROOT / 'android/app/src/main/assets/runtime-third-party-notices.txt',
                   'LICENSES/xboxrecomp-LICENSE.txt': ROOT / 'xboxrecomp/LICENSE',
                   'LICENSES/xboxrecomp-NOTICE.txt': ROOT / 'xboxrecomp/NOTICE',
                   'LICENSES/LGPL-2.1.txt': ROOT / 'xboxrecomp/LICENSES/LGPL-2.1.txt'})
    for name, path in inputs.items():
        target = package / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
    apk_source = ROOT / 'android/app/build/outputs/apk/debug/app-debug.apk'
    with zipfile.ZipFile(apk_source) as apk:
        names = apk.namelist()
        assets = {n for n in names if n.startswith('assets/') and not n.endswith('/')}
        expected_assets = {'assets/importer-third-party-notices.txt', 'assets/runtime-third-party-notices.txt'}
        if assets != expected_assets:
            raise ValueError(f'Unexpected APK assets: {sorted(assets)}')
        libraries = {n for n in names if n.startswith('lib/') and n.endswith('.so')}
        if libraries != {'lib/arm64-v8a/' + n for n in ('libSDL2.so', 'libnfsu2_quest.so', 'libopenxr_loader.so', 'libc++_shared.so')}:
            raise ValueError('Unexpected APK native libraries')
        if any('nfsunder' in n.lower() or n.lower().endswith(('.xbe', '.iso', '.xiso', '.7z', '.mp4')) for n in names):
            raise ValueError('Private game content in APK')
    apk_name = f'NFSU2VR-Quest3-{args.version}.apk'
    shutil.copyfile(apk_source, args.output / apk_name)
    source_name = f'NFSU2VR-Source-{commit[:12]}.zip'
    subprocess.run(['git', '-C', str(source), 'archive', '--format=zip', '--prefix=nfsu2-vr/',
                    '-o', str((args.output / source_name).resolve()), commit], check=True)
    info = {'project': 'Need For Speed Underground 2 VR', 'version': args.version,
            'development_build': True, 'not_fully_tested': True,
            'source_commit': commit, 'source_url': f'https://github.com/DR-89/nfsu2-vr/tree/{commit}',
            'source_archive': source_name, 'platforms': ['Windows x64 PCVR', 'Meta Quest 3 standalone'],
            'dependencies': json.loads((ROOT / 'android/dependencies.json').read_text()),
            'game_assets_included': False,
            'pcvr_files': {n: {'bytes': (package / n).stat().st_size, 'sha256': sha256(package / n)} for n in inputs},
            'quest_apk': {'name': apk_name, 'bytes': apk_source.stat().st_size, 'sha256': sha256(apk_source)},
            'validation': ['43 synthetic native importer assertions', 'Full USA ISO import and SHA-256 verification',
                           'Native first-run dialog, file picker and cancellation inspected', 'Windows PCVR Release build',
                           'Quest debug APK build; no new full gameplay/performance acceptance test']}
    for target in (package / 'BUILD-INFO.json', args.output / 'BUILD-INFO.json'):
        target.write_text(json.dumps(info, indent=2) + '\n', encoding='utf-8')
    zip_name = f'NFSU2VR-PCVR-{args.version}.zip'
    with zipfile.ZipFile(args.output / zip_name, 'x', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(package.rglob('*')):
            if path.is_file():
                relative = path.relative_to(package).as_posix()
                if relative not in inputs and relative != 'BUILD-INFO.json':
                    raise ValueError(f'Unexpected PCVR package file: {relative}')
                archive.write(path, 'NFSU2VR-PCVR/' + relative)
    artifacts = [args.output / n for n in (zip_name, apk_name, source_name, 'BUILD-INFO.json')]
    (args.output / 'SHA256SUMS.txt').write_text(''.join(f'{sha256(p)}  {p.name}\n' for p in artifacts), encoding='ascii')
    print(json.dumps({'source_commit': commit, 'files': [{'name': p.name, 'bytes': p.stat().st_size} for p in artifacts]}, indent=2))


if __name__ == '__main__':
    main()
