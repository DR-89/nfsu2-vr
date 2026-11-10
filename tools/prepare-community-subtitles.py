#!/usr/bin/env python3
"""Extract only the movie text hashes from the pinned English Subtitles Fixed pack.

Uses the owned PC game's subtitle cue files for the allowed hash list. Native
playback uses the Xbox disc's own cue files, original clock, font and layout.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import os
import tempfile

ROOT=Path(__file__).resolve().parents[1]
SHA='1194bc0dc38508eeacb9ed834448bdd99cf85f930f3460ed6db35ab2955cb38f'
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pc-game',type=Path,required=True)
    parser.add_argument('--language',type=Path,default=ROOT/'build/xyz-lan-qa/live-mod-assets/LANGPACKS/english/LANGUAGES/English.bin')
    parser.add_argument('--output',type=Path,default=ROOT/'build/mod-assets')
    args=parser.parse_args();b=args.language.read_bytes()
    if hashlib.sha256(b).hexdigest()!=SHA:raise SystemExit('Unexpected subtitle language pack')
    if struct.unpack_from('<I',b)[0]!=0x39000:raise SystemExit('Missing language chunk')
    size=struct.unpack_from('<I',b,4)[0];header,count,table,pool=struct.unpack_from('<4I',b,8)
    assert header==16 and 8+size<=len(b) and 16<=table<=pool<size and table+count*8<=pool
    texts={}
    for i in range(count):
        key,offset=struct.unpack_from('<II',b,8+table+i*8)
        assert offset<size-pool
        end=b.index(0,8+pool+offset,8+size);texts[key]=b[8+pool+offset:end]
    keys=set();movies=0
    for path in sorted((args.pc_game/'SUBTITLES').iterdir()):
        if not path.is_file():continue
        cues=path.read_bytes()
        if not cues or len(cues)%8 or len(cues)>4096:raise SystemExit('Invalid movie cue file')
        last=-1
        for at in range(0,len(cues),8):
            second,padding,key=struct.unpack_from('<HHI',cues,at)
            assert second>=last;last=second;keys.add(key)
        assert last==65535;movies+=1
    # 0x001A20BA is the player's explicit blank/clear marker, not a text.
    keys.discard(0x001a20ba)
    assert movies>=10 and 50<=len(keys)<=256 and keys<=texts.keys()
    result=bytearray(12+8*len(keys));result[:4]=b'NVS1';struct.pack_into('<I',result,4,len(keys))
    for i,key in enumerate(sorted(keys)):
        text=texts[key];assert len(text)<=1023
        struct.pack_into('<II',result,12+8*i,key,len(result));result+=text+b'\0'
    struct.pack_into('<I',result,8,len(result))
    target=args.output/'TEXPACKS/english-subtitles.bin';target.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=target.parent,delete=False) as f:
        temporary=Path(f.name);f.write(result)
    try:os.replace(temporary,target)
    finally:temporary.unlink(missing_ok=True)
    print(f'English Subtitles Fixed: {len(keys)} texts for {movies} movies, {len(result)} bytes')
if __name__=='__main__':main()
