#!/usr/bin/env python3
"""Stage the privately converted Corsa/106 pack for PCVR or an explicit Quest.

Prepare with prepare-community-regional.py PC_GAME build/optional-mod-assets/REGIONAL.
No game archive or saved career is changed. Close the Quest app before updating.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
PACKAGE='org.nfsu2.quest'
NAMES=tuple(f'CARS/{model}/{name}.BIN' for model in ('PEUGOT106','CORSA')
            for name in ('GEOMETRY','TEXTURES','VINYLS'))


def sha(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def verified(source):
    metadata=json.loads((source/'manifest.json').read_text(encoding='utf-8'))
    if metadata.get('format')!='NREG1' or set(metadata.get('files',{}))!=set(NAMES):
        raise ValueError('Incomplete regional pack manifest')
    files=[]
    for name in NAMES:
        path=source/name;spec=metadata['files'][name]
        if path.stat().st_size!=spec['bytes'] or sha(path)!=spec['sha256']:
            raise ValueError(f'Prepared regional file differs: {name}')
        files.append((path,name,spec['sha256']))
    return files


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=ROOT/'build/optional-mod-assets/REGIONAL')
    target=parser.add_mutually_exclusive_group(required=True)
    target.add_argument('--pcvr',type=Path,help='Directory containing the PCVR executable')
    target.add_argument('--serial',help='Quest ADB device serial')
    parser.add_argument('--adb',default='adb')
    args=parser.parse_args()
    files=verified(args.source)
    if args.pcvr:
        for source,name,expected in files:
            dest=args.pcvr/'REGIONAL'/name
            if source.resolve()==dest.resolve():continue
            dest.parent.mkdir(parents=True,exist_ok=True)
            with tempfile.NamedTemporaryFile(dir=dest.parent,delete=False) as f:temp=Path(f.name)
            temp.unlink()
            try:
                try:os.link(source,temp)
                except OSError:shutil.copyfile(source,temp)
                if sha(temp)!=expected:raise ValueError(f'Staged file differs: {name}')
                os.replace(temp,dest)
            finally:temp.unlink(missing_ok=True)
        shutil.copyfile(args.source/'manifest.json',args.pcvr/'REGIONAL/manifest.json')
    else:
        base=[args.adb,'-s',args.serial]
        def adb(*command,check=True):
            return subprocess.run([*base,*command],text=True,capture_output=True,check=check)
        if adb('shell','pidof',PACKAGE,check=False).stdout.strip():
            parser.error('Close NFSU2 Development before installing the regional pack')
        folder=f'/sdcard/Android/data/{PACKAGE}/files/REGIONAL'
        staging='/data/local/tmp/nfsu2-regional.bin'
        for source,name,expected in files:
            dest=folder+'/'+name;temporary=dest+'.new'
            adb('shell',f'run-as {PACKAGE} mkdir -p '+shlex.quote(dest.rsplit('/',1)[0]))
            adb('push',str(source.resolve()),staging)
            adb('shell',f'run-as {PACKAGE} cp {shlex.quote(staging)} {shlex.quote(temporary)}')
            actual=adb('shell',f'run-as {PACKAGE} sha256sum {shlex.quote(temporary)}').stdout.split()[0]
            if actual!=expected:raise RuntimeError(f'Device checksum differs: {name}')
            adb('shell',f'run-as {PACKAGE} mv {shlex.quote(temporary)} {shlex.quote(dest)}')
            print(f'Installed and verified {name}',flush=True)
    print('Regional pack verified. Enable European Cars under Community Mods, save and restart.')


if __name__=='__main__':main()
