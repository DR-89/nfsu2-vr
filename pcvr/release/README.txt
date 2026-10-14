Need For Speed Underground 2 VR - Windows PCVR development build
================================================================

DEV BUILD - NOT 100% TESTED. Bugs, crashes, rendering/audio issues,
stuttering and performance problems may occur. Stable VR frame rates
are not guaranteed.

1. Extract the entire ZIP into a writable folder. Do not run inside the ZIP.
2. Install a current GPU driver with Vulkan support and connect your headset
   through Virtual Desktop. This development configuration uses VDXR.
3. Run Start-PCVR.cmd. If game data is missing, Game data setup opens.
4. Click Select ISO... and choose your original Xbox USA / NTSC-U ISO/XISO.
   PC and European discs are unsupported. Unpack ZIP/7z archives first.
5. Wait for extraction and SHA-256 verification. PCVR starts automatically.
   Allow about 2.5 GB free in addition to your source ISO. Saved games remain
   intact. Cancel closes setup without starting VR.

Game files are installed into game/ beside the launcher. Future launches
reuse them. The source ISO is never deleted or changed. Do not redistribute
the imported game directory, your ISO or your saves.

Direct EXE startup also shows setup when data is absent. Without the launcher
it uses a complete game/ in the working directory or beside the EXE, otherwise
%LOCALAPPDATA%\NFSU2VR\game. Use Start-PCVR.cmd for the supplied tested preset.

Required system software: Windows 10/11 x64, Vulkan-capable graphics driver,
and an installed/configured OpenXR runtime. Virtual Desktop/VDXR is the
development-tested configuration. Windows N editions need Media Feature Pack.
Driver files and the headset runtime are installed separately; they cannot be
bundled as application DLLs. No Python, Java or separate ISO extractor is needed.

Bundled: port EXE, SDL2, OpenXR loader, Microsoft Visual C++ runtime DLLs,
development preset, launcher and dependency license notices. The original
Xbox executable, disc images, game assets and saves are NOT included.

Controls: Touch face buttons map to Xbox face buttons; left menu = Start;
right stick navigates menus; both stick clicks recenter; grips = White/Black.

If startup fails, read logs/stderr.log. Logs from the previous launch are
replaced. Performance and streaming quality depend on your PC and connection.
Source revision and binary hashes are recorded in BUILD-INFO.json.

This mod is based on the work of antoxa2584x:
https://github.com/antoxa2584x/nfsu2-sw
Recompilation toolkit: https://github.com/sp00nznet/xboxrecomp
VR source and platform build instructions: https://github.com/DR-89/nfsu2-vr
For modified-runtime builds, use the matching source archive and pinned
dependencies, provide your own disc, regenerate the private C and rebuild
with tools/build-pcvr.ps1. The repository retains original license notices.

Microsoft Visual C++ runtime DLLs are Microsoft redistributable components
and remain under Microsoft's terms, not the port/toolkit licenses.
This unofficial fan project is not endorsed by EA, Meta or Microsoft.
