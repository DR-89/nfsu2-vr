# Need For Speed Underground 2 VR — PCVR & Meta Quest 3

![PCVR](https://img.shields.io/badge/PCVR-Windows_x64-blue)
![Meta Quest 3](https://img.shields.io/badge/Meta_Quest_3-Standalone_ARM64-blue)
![Status](https://img.shields.io/badge/Status-DEV_BUILD-orange)
![Testing](https://img.shields.io/badge/Testing-Incomplete-orange)
![Distribution](https://img.shields.io/badge/Distribution-Source_and_dev_builds-green)

An experimental VR port of **Need for Speed: Underground 2**, based on the
original Xbox USA / NTSC-U version. It uses static recompilation with
**xboxrecomp**, **Vulkan** rendering and **OpenXR** tracking and presentation.

This VR mod is based on the work of **antoxa2584x** in
**[nfsu2-sw](https://github.com/antoxa2584x/nfsu2-sw)**. The PCVR and Meta Quest 3
adaptations build on that project's Xbox recompilation and runtime work.

> [!WARNING]
> **DEV BUILD — NOT 100% TESTED.** This project is under active development.
> Bugs, crashes, freezes, visual glitches, audio issues, stuttering and
> performance problems may occur. Features and compatibility are incomplete,
> and stable VR frame rates are not guaranteed. This is not a stable release.

> [!IMPORTANT]
> **No original game content is included.** You must supply your own supported
> Xbox USA / NTSC-U disc image or extracted game files. The development builds
> contain the port and its runtime, but no disc images, original Xbox executable,
> retail game assets, music, movies or saves. Building from source additionally
> requires generating the private game code locally.

## Platforms

| Target | Runtime | Development status |
| --- | --- | --- |
| **PCVR — Windows x64** | Vulkan + OpenXR; development testing uses Virtual Desktop / VDXR with Quest 3 | Stereo races and Touch controls have been exercised. Bugs, audio glitches and frame-time spikes remain; stable 72 FPS across gameplay has not been established. |
| **Meta Quest 3 — standalone** | Native Android ARM64, Vulkan + OpenXR | Boots and races directly on the headset with tracked stereo and Touch controls. Rendering issues, stutters and performance limitations remain. |

Other PC OpenXR runtimes and other Quest models have not been established as
supported configurations. Inherited Linux and Nintendo Switch code remains in
the tree; the focus of this development publication is PCVR and Meta Quest 3.

## Development downloads

The [current development release](https://github.com/DR-89/nfsu2-vr/releases/tag/dev-2026-10-14)
includes:

- [Meta Quest 3 APK](https://github.com/DR-89/nfsu2-vr/releases/download/dev-2026-10-14/NFSU2VR-Quest3-dev-2026-10-14.apk):
  sideload the development APK, then use its setup panel to import your own
  supported Xbox USA ZIP, 7z or ISO/XISO.
- [Windows PCVR package](https://github.com/DR-89/nfsu2-vr/releases/download/dev-2026-10-14/NFSU2VR-PCVR-dev-2026-10-14.zip):
  extract the whole ZIP to a writable folder and run **Start-PCVR.cmd**.
  If game data is missing, **Game data setup** opens automatically. Select your
  Xbox USA ISO/XISO; progress and cancellation are available. All 12 required
  files are verified before VR starts. Saved games are preserved.
- Matching source archive, build information and SHA-256 checksums.

The PCVR ZIP includes the EXE, SDL/OpenXR loader DLLs, Visual C++ runtime DLLs,
launcher, preset and notices. Install your GPU's Vulkan driver and a compatible
OpenXR runtime separately; development testing uses Virtual Desktop / VDXR.
No Python, Java or external ISO extractor is required to run the PCVR importer.
See the included README for system requirements. These downloads remain
**development builds, not fully tested**, with possible bugs and performance issues.

## Get the source and prepare your own game files

The examples below use Windows PowerShell. Install Git and Python 3.13 first.

```powershell
git clone https://github.com/DR-89/nfsu2-vr.git
cd nfsu2-vr
py -3.13 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r tools/quest-requirements.txt
```

Place your extracted, unmodified Xbox USA / NTSC-U disc in `game/`, containing
`default.xbe` and the complete `NFSUNDER/` directory. The PC retail release and
European Xbox release are not supported by these title-specific addresses.
The repository does not supply or download the game.

```powershell
.\.venv\Scripts\python.exe tools/inspect_game.py .\game
.\.venv\Scripts\python.exe tools/regen.py .\game --gen-dir .\gen
```

The generator requires an empty output directory. Keep `game/`, `gen/`, build
outputs and data archives private; they are excluded from Git.

## Build for PCVR

Requires Windows x64, Visual Studio 2022 C++ tools, CMake, Ninja, Vulkan headers
and a working Vulkan graphics driver. The current launcher is configured for
Virtual Desktop / VDXR.

Fetch and verify the pinned source dependencies once:

```powershell
$dependencies = Get-Content android/dependencies.json -Raw | ConvertFrom-Json
foreach ($item in $dependencies.PSObject.Properties) {
    $destination = "build/deps/$($item.Name)"
    $dependency = $item.Value
    if (-not (Test-Path -LiteralPath $destination)) {
        git clone --depth 1 --branch $dependency.tag $dependency.url $destination
        if ($LASTEXITCODE) { throw "Dependency clone failed: $($item.Name)" }
    }
    $revision = git -C $destination rev-parse HEAD
    if ($LASTEXITCODE -or $revision -ne $dependency.revision) {
        throw "Dependency revision mismatch: $($item.Name)"
    }
}
.\tools\build-pcvr.ps1 -VulkanHeaders 'C:/VulkanSDK/<version>/Include'
.\tools\start-pcvr.ps1
```

Connect the headset through Virtual Desktop before launching. The local output
is `build/pcvr/game/nfsu2-pcvr.exe`; keep its required SDL and OpenXR DLLs beside
it. See [PCVR build, configuration and testing notes](pcvr/README.md).

## Build for Meta Quest 3 standalone

Requires CMake, Android SDK platform/build-tools 35, NDK 29, JDK 17+ and
Gradle 8.11.1, in addition to the common setup above. The build scripts fetch
the pinned native dependencies automatically.

```powershell
.\tools\build-quest.ps1 -Sdk 'C:\path\to\android-sdk' `
  -Ndk 'C:\path\to\android-sdk\ndk\29.0.14206865' `
  -JavaHome 'C:\path\to\jdk' -Gradle 'C:\path\to\gradle.bat'
```

This produces a local development APK at
`android/app/build/outputs/apk/debug/app-debug.apk`. It requires your separate
game data. The setup panel can import your supported local ZIP, 7z or Xbox
ISO/XISO. See [Quest build, import, installation and testing notes](android/README.md).

## Controls

- Touch A/B/X/Y map to Xbox A/B/X/Y.
- The left menu button maps to Start; the right stick provides menu navigation.
- The left stick and triggers map to the corresponding Xbox controls.
- Left/right grips map to White/Black.
- Press both sticks together to recenter the seated view.

## Source layout and development notes

| Path | Contents |
| --- | --- |
| `android/` | Quest application, importer and shared Vulkan/OpenXR integration |
| `pcvr/` | Windows host, compatibility code and development preset |
| `src/` | Port startup and handwritten runtime adaptations |
| `xboxrecomp/` | Vendored recompilation toolkit and host runtime |
| `tools/` | Build, code-generation, import and performance-analysis tools |
| `tests/` | Focused regression tests |

The platform documentation contains historical measurements and references to
private logs and captures. Those files are not part of this publication, and
individual successful tests do not imply complete gameplay coverage.

See [publication scope](PUBLICATION.md),
[inherited Switch/Linux documentation](README-UPSTREAM.md),
[performance notes](PERF_NOTES.md) and [Vulkan notes](VULKAN_NOTES.md).
When reporting a bug, include the platform, source revision, runtime, settings
and steps to reproduce it. Do not attach retail game files or generated game code.

## Credits and licenses

Based on [antoxa2584x/nfsu2-sw](https://github.com/antoxa2584x/nfsu2-sw) and
[sp00nznet/xboxrecomp](https://github.com/sp00nznet/xboxrecomp), with the current
PCVR and Quest 3 development work maintained by **DR-89**.
See [THIRD_PARTY.md](THIRD_PARTY.md) for upstream revisions, credits and license
notices. Existing source and dependency licenses are retained.

This is an unofficial fan project, not affiliated with or endorsed by
Electronic Arts, Meta or Microsoft. Need for Speed and Underground are
trademarks of Electronic Arts. No original game content is distributed.
