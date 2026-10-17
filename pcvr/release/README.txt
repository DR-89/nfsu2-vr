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

Controls (Touch controllers):
- Left menu button: pause/Start. Right stick navigates menus.
- Right stick click: cycle cameras, including entering/leaving Cockpit View.
- Left stick click: hold rear view outside the cockpit; disabled inside it.
- Both stick clicks: recenter the view and release wheel grips.
- A: nitro while driving. B: handbrake while driving.
- Right stick up/down: shift up/down with Manual transmission; return it to
  center between shifts. Right stick left reads SMS; right enters a race/shop.
  Menu confirm/back retain their normal mapping.
- Grip near any rim point: one- or two-hand motion steering in the cockpit,
  including crossed hands. Default wheel travel is 360 degrees (+/-180).
- Releasing both grips retains steering. Move the left stick at least 25%
  sideways to resume ordinary stick steering. A held glove stays attached to
  its initial rim point; release/regrip to choose another point.
- Controller vibration follows the game's rumble; enable Vibration in-game.

The shared anthracite/blue Tripo cabin includes doors, front seats, rear bench,
floor, dashboard and animated wheel. Its extra headliner/window pillars were
removed to fit the original car body and hood. Three original mirror surfaces
use sectors of one live rear camera. The speed display sits behind the wheel,
with the map on the center console; pause menus stay on one complete panel.
Keep the entire bundled cockpits/ folder, including the racing gloves.
The cabin now has base seat/body fitting for all 29 stock cars. Miata and 350Z
omit the rear bench. Original driver-proxy cards and the player's window glass
are hidden only in Cockpit View; exterior tint, body and mirrors are retained.
The dashboard speed display, console map and recent mirror/tint corrections
were accepted on native Quest. PCVR shares this code but has not had a fresh
headset acceptance test for this release. Every tuning combination is not tested.

VR Settings:
- Pause during a race, then press X. The entry is available only at the root
  pause menu. Use a stick/D-pad to select and adjust; B saves and returns.
- Steering Sensitivity: 50-300%; Wheel Rotation: 180-1080 degrees total.
- Seat Height: +/-35 cm; Seat Distance: +/-40 cm. Positive distance moves back.
- Render Resolution: select a per-eye preset, then save and restart to apply.
  Higher values can reduce performance. Stable 72 FPS is not guaranteed.
- Settings are saved under %APPDATA%\OpenSourceDevDirk\NFSU2VR.

Mapped SDL USB/Bluetooth gamepads are supported with the same driving layout.
Physical gamepad/headset combinations still need broader hardware testing.

Connect Virtual Desktop before starting. Transient OpenXR startup failures
are retried; if a connection dialog appears, reconnect the headset and choose
Retry. This recovery path still needs broader PCVR headset testing.

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
