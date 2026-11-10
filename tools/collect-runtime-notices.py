#!/usr/bin/env python3
"""Collect native dependency license texts into the APK's bundled notices."""
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ndk', type=Path, required=True)
    args = parser.parse_args()
    sources = [
        ('xboxrecomp license', ROOT / 'xboxrecomp/LICENSE'),
        ('xboxrecomp third-party attribution', ROOT / 'xboxrecomp/NOTICE'),
        ('LGPL 2.1', ROOT / 'xboxrecomp/LICENSES/LGPL-2.1.txt'),
        ('FFmpeg 7.1.5 (video and music decoders), https://ffmpeg.org', ROOT / 'build/deps/FFmpeg/COPYING.LGPLv2.1'),
        ('SDL', ROOT / 'build/deps/SDL/LICENSE.txt'),
        ('SDL yuv2rgb', ROOT / 'build/deps/SDL/src/video/yuv2rgb/LICENSE'),
        ('SDL HIDAPI', ROOT / 'build/deps/SDL/src/hidapi/LICENSE.txt'),
        ('SDL HIDAPI BSD', ROOT / 'build/deps/SDL/src/hidapi/LICENSE-bsd.txt'),
        ('SDL HIDAPI original', ROOT / 'build/deps/SDL/src/hidapi/LICENSE-orig.txt'),
        ('OpenXR SDK', ROOT / 'build/deps/OpenXR-SDK/LICENSE'),
        ('OpenXR attribution', ROOT / 'build/deps/OpenXR-SDK/COPYING.adoc'),
        ('OpenXR JsonCpp', ROOT / 'build/deps/OpenXR-SDK/src/external/jsoncpp/LICENSE'),
        ('OpenXR jnipp', ROOT / 'build/deps/OpenXR-SDK/src/external/jnipp/LICENSE'),
        ('nfsu2-web free-roam core (16b23865f09dc41b9533635180e6d3fd34c45a6d) and name matching (304978012da55b273c143f9a8bcec7d1d298a873), https://github.com/th0mk/nfsu2-web', ROOT / 'src/community/vendor/roam/LICENSE'),
        ('nfsu2-web SHA-256 and keyphrase format (304978012da55b273c143f9a8bcec7d1d298a873)', ROOT / 'src/community/vendor/auth/LICENSE'),
        ('Keyphrase wordlist attribution and adaptation', ROOT / 'src/community/vendor/auth/WORDS-NOTICE.txt'),
        ('Visual Reputation Remover, Denilson das Mercês Amorim (LINK/2012), https://github.com/thelink2012/novisualrep', ROOT / 'src/community/vendor/visualrep/LICENSE'),
        ('Underground 2 Transmission Whine, ropi, https://github.com/ropi336/UG2WhineGen', ROOT / 'src/community/vendor/whine/NOTICE.txt'),
        ('NTXP raw-deflate decoder, nfsu2-web', ROOT / 'src/community/vendor/texpacks/LICENSE'),
        ('NTXP decoder adaptation', ROOT / 'src/community/vendor/texpacks/NOTICE.txt'),
        ('Optional community texture artwork', ROOT / 'src/community/vendor/texpacks/ARTWORK-NOTICE.txt'),
        ('Crash Damage native adaptation, Crashing Foreign Cars by FRS Mods', ROOT / 'src/community/vendor/damage/NOTICE.txt'),
        ('Crash Damage source license', ROOT / 'src/community/vendor/damage/LICENSE'),
        ('Local optional asset preparation codecs', ROOT / 'src/community/vendor/assettools/NOTICE.txt'),
        ('Asset preparation codec license', ROOT / 'src/community/vendor/assettools/LICENSE'),
        ('cJSON 1.7.19, https://github.com/DaveGamble/cJSON', ROOT / 'src/community/vendor/cjson/LICENSE'),
        ('OkHttp 4.12.0 (Quest network transport), Copyright Square, Inc., https://github.com/square/okhttp', ROOT / 'src/community/vendor/android-licenses/okhttp-LICENSE.txt'),
        ('OkHttp public suffix data attribution', ROOT / 'src/community/vendor/android-licenses/publicsuffix-NOTICE.txt'),
        ('Okio 3.6.0 (Quest network transport), Copyright Square, Inc., https://github.com/square/okio', ROOT / 'src/community/vendor/android-licenses/okio-LICENSE.txt'),
        ('Kotlin standard library 1.9.10, Copyright JetBrains s.r.o. and contributors, https://github.com/JetBrains/kotlin', ROOT / 'src/community/vendor/android-licenses/kotlin-LICENSE.txt'),
        ('glslang', ROOT / 'build/deps/glslang/LICENSE.txt'),
        ('LLVM / Android C++ runtime', args.ndk / 'toolchains/llvm/prebuilt/windows-x86_64/NOTICE'),
    ]
    blocks = [
        'Need For Speed Underground 2 VR - Native runtime notices\n\n'
        'Based on https://github.com/antoxa2584x/nfsu2-sw and\n'
        'https://github.com/sp00nznet/xboxrecomp .\n'
        'Port/runtime source, build instructions, and exact dependency revisions:\n'
        'https://github.com/DR-89/nfsu2-vr\n'
        'Each binary release identifies its matching source commit and source archive.\n'
        'To rebuild/relink with modified LGPL components, follow the platform build\n'
        'instructions using your own supported disc and locally generated code.\n'
        'Android importer dependency notices are in importer-third-party-notices.txt.\n'
        'The following license texts retain their original terms and attribution.\n'
    ]
    for title, path in sources:
        blocks.append('\n' + '=' * 72 + '\n' + title + '\n' + '=' * 72 + '\n' + path.read_text(encoding='utf-8'))
    output = ROOT / 'android/app/src/main/assets/runtime-third-party-notices.txt'
    output.write_text('\n'.join(blocks) + '\n', encoding='utf-8')
    print(f'Runtime notices: {output.name} ({output.stat().st_size} bytes)')


if __name__ == '__main__':
    main()
