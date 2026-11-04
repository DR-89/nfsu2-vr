Need For Speed Underground 2 VR - Windows PCVR development build
================================================================

DEV BUILD - NOT 100% TESTED. Bugs, crashes, rendering/audio issues,
stuttering and performance problems may occur. Stable VR frame rates
are not guaranteed.

1. Extract the entire ZIP into a writable folder. Do not run inside the ZIP.
2. Install a current GPU driver with Vulkan support and connect your headset
   through Steam Link and SteamVR, or through Virtual Desktop and VDXR.
3. Run Start-PCVR.cmd to use your active OpenXR runtime. To explicitly select
   SteamVR, run Start-PCVR.cmd -SteamVR. On first launch, Game data setup opens.
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
and an installed/configured OpenXR runtime. Development testing used SteamVR
and Virtual Desktop/VDXR. Windows N editions need Media Feature Pack.
Driver files and the headset runtime are installed separately; they cannot be
bundled as application DLLs. No Python, Java or separate ISO extractor is needed.

Bundled: port EXE, SDL2, OpenXR loader, Microsoft Visual C++ runtime DLLs,
development preset, launcher and dependency license notices. The original
Xbox executable, disc images, game assets and saves are NOT included.

Controls (Touch controllers):
- Left menu button: pause/Start. Right stick navigates menus.
- Steam Link: hold Y on the left Touch controller for 0.65 seconds while
  driving to pause. Tap Y for its usual action. Gamepad Start also pauses.
- Right stick click: cycle cameras, including entering/leaving Cockpit View.
- Left stick click: hold rear view outside the cockpit; disabled inside it.
- Both stick clicks: recenter the view and release wheel grips.
- A: nitro while driving. B: handbrake while driving.
- Right stick up/down: shift up/down with Manual transmission; return it to
  center between shifts. Right stick left reads SMS; right enters a race/shop.
- With Manual transmission in Cockpit View, hold Grip on the shift knob and
  move it through neutral into a slot. Five-speed reverse is behind fifth.
  Six-speed reverse is one lane to the right of sixth. Either hand can shift.
  For a manual standing start with VR controllers, push the lever fully
  forward in the left rail to engage first. You can do this during the countdown.
  Menu confirm/back retain their normal mapping.
- Grip near any rim point: one- or two-hand motion steering in the cockpit,
  including crossed hands. Default wheel travel is 360 degrees (+/-180).
- Grip the center mirror to tilt it. Gripping either outside mirror adjusts
  both together. Release Grip to save the angles for this car.
  In VR Settings, select Reset Car Mirrors to restore their normal aim.
- The next race restores the last camera and mirror setup. Under the
  minimap, drag the EA TRAX volume slider with Grip or squeeze Grip once on
  previous, play/pause or next. Title and artist appear below the buttons.
  Turning the slider down to silence keeps the song playing.
- Right and left trigger travel animates the accelerator and brake. Manual
  shifts depress the clutch pedal and it returns as the shift completes.
- October 18 steering fix: small hand-driven wheel turns now compensate for
  the original game's 24% stick deadzone. Stick/gamepad deadzones are unchanged;
  switching between the stick and wheel preserves the effective steering.
- Set Wheel Return Speed in VR Settings (Off to 540 degrees/second, default
  240). Release both grips to let the wheel center. Catch it with either hand.
  Move the left stick at least 25% sideways to resume ordinary stick steering.
- Controller vibration follows the game's rumble; enable Vibration in-game.

The shared anthracite/blue Tripo cabin includes doors, front seats, rear bench,
floor, dashboard and animated wheel. Its extra headliner/window pillars were
removed to fit the original car body and hood. Three original mirror surfaces
reflect a wide live rear-camera capture per eye. The speed display sits behind the wheel,
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
