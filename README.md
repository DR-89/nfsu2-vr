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
  Follow the [Quest installation guide](#installing-on-meta-quest-3) below.
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

## Installing on Meta Quest 3

The Quest build runs standalone on the headset. Install the APK, copy your game
data to the Quest, then import it in the app. This is an experimental development
build; rendering and performance issues remain. Quest 3 is the verified target.

### 1. Get the APK and prepare your game data

- Download the `NFSU2VR-Quest3-<version>.apk` asset from the
  [project releases](https://github.com/DR-89/nfsu2-vr/releases).
  If building it yourself, follow the [Quest build instructions](android/README.md#build-on-windows);
  the output is `android/app/build/outputs/apk/debug/app-debug.apk`.
- Have a computer, a USB-C **data** cable and your own unmodified **original Xbox
  USA / NTSC-U** copy of Need for Speed: Underground 2 ready.
- Prepare one of the following supported inputs:

| Input | Contents |
|---|---|
| `.iso` or `.xiso` | An original Xbox disc image of the supported USA release. Supported full-disc images are read directly. |
| `.zip` or `.7z` with game files | `default.xbe` and the complete `NFSUNDER/` folder, including `ZDIR.BIN` and `ZZDATA0.BIN` through `ZZDATA9.BIN`. A common enclosing folder is accepted. |
| `.zip` or `.7z` with a disc image | Exactly one Xbox `.iso` or `.xiso` inside the archive. The Quest unpacks it automatically. |

Use complete, unencrypted files. PC and European editions, modified game files,
RAR archives and split archive parts are unsupported. For a RAR or split archive,
extract it on your computer first, then transfer the resulting ISO or repack the
complete extracted game files as ZIP/7z. Renaming an extension does not convert it.

Allow space for the source file **and** temporary extraction. After the source
has been copied to the Quest, these are useful free-space estimates:

| Source already on the Quest | Additional free space for import |
|---|---|
| ZIP/7z of extracted game files | About **5 GB** |
| Full Xbox ISO of about 7.8 GB | About **11 GB** |
| 7z of about 5.9 GB containing a full 7.8 GB ISO | About **17 GB** |

Actual requirements depend on the archive size and existing saves. The importer
temporarily copies the source, unpacks any contained ISO and installs about
2.4 GB of verified game files. Keep some extra space available when reimporting.

### 2. Enable sideloading and connect the Quest

1. Follow [Meta's device setup guide](https://developers.meta.com/horizon/documentation/native/android/mobile-device-setup/)
   to create or join a developer team and verify the Meta account used on your
   headset.
2. In the **Meta Horizon** phone app, select your paired headset, open
   **Headset Settings > Developer Mode**, and enable it. Menu locations can vary
   by app version.
3. On Windows, install the **Oculus ADB Drivers** linked in Meta's guide.
4. Connect the powered-on Quest to your computer with the USB data cable. Put on
   the headset and accept **Allow USB debugging**. On your own computer, you can
   select **Always allow from this computer**.

### 3. Install the APK

**Using SideQuest:**

1. Install the desktop **Advanced Installer** from the
   [SideQuest setup page](https://sidequestvr.com/setup-howto).
2. Open SideQuest and wait until it shows the Quest as connected and authorized.
3. Use **Install APK file from folder** / **Install APK**, select the downloaded
   `NFSU2VR-Quest3-<version>.apk`, and wait for the installation task to finish.

**Using ADB instead:**

Download and extract Google's [Android SDK Platform Tools](https://developer.android.com/tools/releases/platform-tools).
Open PowerShell in the extracted `platform-tools` folder and run:

```powershell
.\adb.exe devices
.\adb.exe -s YOUR_QUEST_SERIAL install -r "C:\path\to\NFSU2VR-Quest3-<version>.apk"
```

Replace `YOUR_QUEST_SERIAL` with the serial shown by `devices`, and replace the
APK path, including `<version>`, with your actual filename. The device state
must be `device`; if it says `unauthorized`, accept the debugging prompt inside
the headset. Wait for `Success`. On macOS/Linux, use `./adb` instead of
`.\adb.exe` and adjust the local paths. See the
[ADB command reference](https://developer.android.com/tools/adb) for details.

### 4. Copy the ISO or archive to the Quest

Choose **one** of the following transfer methods. Put the file in the Quest's
shared **Download** folder (`/sdcard/Download/`), which the Android file picker
may display as **Downloads**. Wait for the entire transfer to finish before
starting the import.

**Windows File Explorer (USB):**

1. With the Quest connected, accept the headset's USB file/data access prompt
   or open its USB notification to enable file transfer. This permission is
   separate from USB debugging. If the prompt is missing, enable **MTP
   Notification** in the headset's Developer settings and reconnect the cable.
2. In File Explorer, open **This PC > Quest 3 > Internal shared storage > Download**.
3. Copy your `.iso`, `.xiso`, `.zip` or `.7z` file into that folder.

**SideQuest file manager:**

Open SideQuest's file manager, browse to `sdcard/Download`, and use its upload or
file-transfer control to copy your ISO or archive from the computer. For this
step, use the file manager; the APK installation button is for installing the app.

**ADB (Windows PowerShell):**

From the same `platform-tools` folder, run the command matching your file:

```powershell
# Direct ISO import:
.\adb.exe -s YOUR_QUEST_SERIAL push "C:\Games\NFSU2-USA.iso" /sdcard/Download/NFSU2-USA.iso

# Or transfer a ZIP/7z archive (this example uses 7z):
.\adb.exe -s YOUR_QUEST_SERIAL push "C:\Games\NFSU2-USA.7z" /sdcard/Download/NFSU2-USA.7z
```

Replace the serial and source filename with your own. Keep the matching file
extension in the destination path. No manual copying into `Android/data` is
needed: the app installs the game files into its own private storage during import.

### 5. Import the game data and play

1. In the Quest's app library, select **Unknown Sources** and open
   **NFSU2 Quest Development**. The first launch opens the game data setup panel.
   You can also open **NFSU2 · Game data** to access setup directly.
2. Press **Select archive**, browse to **Downloads** (or the internal storage's
   `Download` folder), and select your ISO, XISO, ZIP or 7z.
3. Wait for **Import complete**. The app extracts and verifies the supported game
   files. A large 7z containing an ISO can take several minutes. Leave the setup
   panel open; closing it can cancel the import. Progress and **Cancel import**
   are available while it runs.
4. Press **Start game**. Later launches through **NFSU2 Quest Development** start
   VR directly. The computer and USB connection are no longer needed to play.
5. After a successful import, you may delete the source ISO/archive from the
   Quest's `Download` folder to recover space. The app keeps its installed copy
   and never deletes your source file automatically.

To import different data later, close the running game, open **NFSU2 · Game data**
and select **Reimport game data**. Existing saved games and settings are kept.
Update the APK by installing over the existing app with SideQuest or `adb install -r`
using a build signed with the same key. **Uninstalling the app or clearing its
storage deletes the installed game data and saves.**

### Installation troubleshooting

| Problem | What to check |
|---|---|
| SideQuest/ADB cannot see the headset | Check Developer Mode, the USB debugging prompt and the Windows ADB driver. Try another data cable or USB port. |
| File Explorer does not show Quest storage | Accept the separate USB file access prompt; enable MTP Notification if needed. You can also transfer with SideQuest or ADB. |
| The APK was copied but no app appears | Install it using SideQuest's APK installer or `adb install -r`, then check the library's Unknown Sources filter. |
| ISO/archive is missing from the picker | Confirm the transfer finished and browse to internal storage > Download rather than only the Recent view. |
| Not enough storage | Free space for both the source and the temporary files listed above. An archive of extracted game files uses less temporary space than an archive containing a full ISO. |
| Incompatible `default.xbe` or failed verification | Use the complete, unmodified Xbox USA / NTSC-U release and recopy the file if the transfer was incomplete. |
| 7z needs too much memory | Repack with a dictionary of 64 MiB or less, use ZIP, or extract the ISO on your computer and transfer that instead. |
| Game still running | Close the game before reimporting and press **Check again** in setup. |
| APK update reports a signature mismatch | Use an APK signed with the same key as the installed build. Preserve your saves before considering an uninstall. |

For build details, importer limits and developer installation tools, see the
[Quest development notes](android/README.md).

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
