#!/usr/bin/env python3
"""Stage FRS Mods' four scratch textures from the pinned public source pack.

The native loader converts the PC texture headers to Xbox DXT5 headers. Game
archives remain untouched. Keep the source author's notice with staged files.
"""
import argparse
import hashlib
import os
from pathlib import Path
import tempfile

ROOT=Path(__file__).resolve().parents[1]
SHA='9e76c6dbea1205f346e82028d74ab1ecad610014502ca27bb1db1fb227d2ecc4'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=ROOT/'upstream/nfsu2-web-current/tools/damage/scratches.tpk')
    parser.add_argument('--output',type=Path,default=ROOT/'build/mod-assets')
    args=parser.parse_args()
    data=args.source.read_bytes()
    if len(data)!=1399408 or hashlib.sha256(data).hexdigest()!=SHA:
        raise SystemExit('Unexpected scratch pack; no files changed')
    folder=args.output/'TEXPACKS';folder.mkdir(parents=True,exist_ok=True)
    target=folder/'crash-scratches.tpk'
    if not target.exists() or target.read_bytes()!=data:
        with tempfile.NamedTemporaryFile(dir=folder,delete=False) as out:
            temp=Path(out.name);out.write(data)
        try:os.replace(temp,target)
        finally:temp.unlink(missing_ok=True)
    (folder/'CRASH-DAMAGE-NOTICE.txt').write_text((ROOT/'src/community/vendor/damage/NOTICE.txt').read_text()+'\n'+(ROOT/'src/community/vendor/damage/LICENSE').read_text(),encoding='utf-8')
    print(f'Staged four FRS Mods scratch textures: {len(data)} bytes, SHA-256 {SHA}')

if __name__=='__main__':main()
