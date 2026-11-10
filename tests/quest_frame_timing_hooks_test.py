#!/usr/bin/env python3
"""Compile the actual guest presentation hook against a synthetic guest/runtime.

Run with a C compiler on PATH (MSVC cl on Windows, cc elsewhere). The shipped
PCVR preset is exercised as well as explicit rates, unset options and repeated
menu/race transitions. No retail code or headset is needed.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'src/recomp_manual.c')
    parser.add_argument('--cc', default='cl' if os.name == 'nt' else 'cc')
    args = parser.parse_args()
    source = args.source.read_text(encoding='utf-8')
    start = source.index('#if defined(__ANDROID__) || defined(NFSU2_PCVR)\nstatic int quest_60fps')
    end = source.index('extern void sub_001890C0_gen(void);', start)
    hook = source[start:end]
    harness = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quest_frame_timing.h"
#define NFSU2_PCVR 1
static uint32_t race, guest_slot, esp=0x1000, observed;
static int xr_enabled;
static const char *rate, *legacy;
static char *test_getenv(const char *name) {
    if(!strcmp(name,"NFSU2_VR_REFRESH_HZ")) return (char*)rate;
    if(!strcmp(name,"NFSU2_VR_72FPS")) return (char*)legacy;
    if(!strcmp(name,"NFSU2_VR_60FPS")) return "1";
    return NULL;
}
#define getenv test_getenv
#define MEM32(address) (*((address)==0x3A4C50u ? &race : &guest_slot))
static int quest_xr_enabled(void) {return xr_enabled;}
void sub_002F2080_gen(void) {observed=guest_slot;}
''' + hook + r'''
int main(int argc,char **argv) {
    if(argc!=3) return 2;
    rate=strcmp(argv[1],"unset")?argv[1]:NULL;
    legacy=strcmp(argv[2],"unset")?argv[2]:NULL;
    unsigned failures=0;
    /* Start in menu, enter race, leave, reenter, disable XR, enable it again. */
    const unsigned races[]={0,1,0,1,1,1};
    const int xr[]={1,1,1,1,0,1};
    for(unsigned transition=0;transition<6;++transition) {
        race=races[transition]; xr_enabled=xr[transition];
        int paced=xr_enabled && race;
        if(quest_xr_paced()!=paced) ++failures;
        for(unsigned kind=0;kind<32;++kind)
        for(unsigned interval=0;interval<8;++interval)
        for(unsigned immediate=0;immediate<2;++immediate) {
            uint32_t original=0x89abc000u | (immediate<<8) | (interval<<5) | kind;
            uint32_t expected=original;
            if(xr_enabled && kind==1) {
                if(paced) expected=(original & ~224u) | 256u;
                else if(interval>1) expected=(original & ~224u) | 32u;
            }
            guest_slot=original; observed=0;
            sub_002F2080();
            if(observed!=expected || guest_slot!=original) ++failures;
        }
    }
    printf("Guest timing hook refresh=%s legacy=%s: %u failures\n",argv[1],argv[2],failures);
    return failures?1:0;
}
'''
    preset = {}
    for line in (ROOT / 'pcvr/benchmark.env').read_text(encoding='utf-8').splitlines():
        if '=' in line and not line.startswith('#'):
            key, value = line.split('=', 1)
            preset[key.strip()] = value.strip()
    modes = [(preset.get('NFSU2_VR_REFRESH_HZ', 'unset'), preset.get('NFSU2_VR_72FPS', 'unset')),
             ('unset', 'unset'), ('unset', '1'), ('0', '1'),
             ('72', '0'), ('80', '0'), ('90', '0'), ('120', '0')]
    with tempfile.TemporaryDirectory(prefix='nfsu2-timing-') as directory:
        directory = Path(directory)
        test_c = directory / 'timing.c'
        test_c.write_text(harness, encoding='utf-8')
        exe = directory / ('timing.exe' if os.name == 'nt' else 'timing')
        include = ROOT / 'android/runtime'
        if Path(args.cc).stem.lower() == 'cl':
            command = [args.cc, '/nologo', '/std:c11', '/O2', f'/I{include}', str(test_c), f'/Fe{exe}']
        else:
            command = [args.cc, '-std=c11', '-O2', '-I', str(include), str(test_c), '-o', str(exe)]
        subprocess.run(command, cwd=directory, check=True)
        for rate, legacy in modes:
            subprocess.run([str(exe), rate, legacy], check=True)


if __name__ == '__main__':
    main()
