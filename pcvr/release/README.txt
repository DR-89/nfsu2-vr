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
- Left menu button: pause/Start. Either stick or D-pad navigates menus.
- Steam Link: hold Y on the left Touch controller for 0.65 seconds while
  driving to pause. Tap Y for its usual action. Gamepad Start also pauses.
- Right stick click: cycle cameras, including entering/leaving Cockpit View.
- You ride in the cockpit on the way to the starting line and throughout the
  countdown with either transmission. At GO, your saved driving view takes over.
- Left stick click: hold rear view outside the cockpit; disabled inside it.
- Both stick clicks: recenter the view and release wheel grips.
- A: nitro while driving. B: handbrake while driving.
- Right stick up: contextual action (rival challenge / enter event or shop).
  Down: career status. Left: SMS. Right: event/shop shortcut.
  Return to center between actions. The right stick does not shift gears.
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
- Tap EA TRAX buttons with your extended index finger, or slide it along
  the volume control. This also works in a cockpit replay. Tap B (Circle)
  to toggle the timeline. Click the right stick to cycle through six replay
  views. In the driver's seat, hold Grip on a mirror to aim it while the
  replay runs. Both stick clicks together recenter. Hold B for the original
  replay control.
- Put MP3, FLAC, PCM WAV or H.264 MP4 files in Music beside the EXE.
  MP4 videos can have AAC audio or no audio. The game creates the folder
  on launch. Restart after adding files. Music and videos shuffle together
  in driving and replay. Videos play on the 16:9 screen above the radio.
  Audio-only tracks and EA TRAX show a 16-band spectrum there.
  In VR Settings > Radio Soundtrack, Auto keeps the original menu music.
  Custom Only mutes EA TRAX even in menus when playable custom files exist.
  EA TRAX Only selects the original soundtrack. An empty library uses EA TRAX.
- LED strips light the instruments, dash, doors, shifter surround and rear
  shelf. In VR Settings, choose one of eight LED colors or switch the strips
  off. Turn Pulse to Music off for steady lighting. The spectrum and LED pulse
  follow the music even with the radio volume turned all the way down.
- Tap the driver's door-armrest keys to move the seat in 1 cm steps.
- Hold Grip on the cockpit minimap with either free hand to reposition it.
  Tilt or turn your wrist for up to 45 degrees of pitch and yaw. Release
  to save. In career free roam, reach for its blue rim and tap A
  with the right hand or X with the left to open the world map.
- In the pause menu, press X for VR Settings. Adjust Mirror Resolution
  or set the manual H-Pattern Guide to
  Always, Near Shifter or Off. Its dot follows the lever.
- For RPM-based engine rumble, turn on Engine REV Rumble in VR Settings >
  Advanced Haptics / Rumble. Feel a gentle vibration at idle that builds
  toward your car's redline and eases when the revs drop. More throttle
  strengthens the rumble. Grip Feedback limits engine rumble to hands holding
  the wheel or inside the shifter zone. Road and collision feedback still
  reach both hands.
- Master Gain adjusts all game audio.
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
Mirrors show the road and traffic behind you; your own cabin and car are
excluded from the rear capture. The old 2D mirror mask is filtered out to
remove its black bar. Aiming the center mirror leaves the outside views alone.
The movable dash screen now uses cockpit depth correctly in front of or
behind the wheel, including at partial opacity.
Automated mirror and screen-depth checks pass. Final in-headset acceptance
of all changes on both platforms and every tuning combination is pending.

VR Settings:
- Pause during a race, then press X. The entry is available only at the root
  pause menu. Use a stick/D-pad to select and adjust; B saves and returns.
- Cockpit Controls and Community Mods show a scrollbar, direction arrows and
  the number of options above/below. Use stick/D-pad up/down to scroll.
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

The PCVR preset keeps the display rate selected in Virtual Desktop / OpenXR.
It no longer requests 72 Hz. NFSU2_VR_REFRESH_HZ=0 in pcvr.env keeps the runtime
rate; an explicit 72, 80, 90 or 120 requests that supported rate instead.
Display refresh is not a guarantee of the game's rendered frame rate.

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

Cockpit Controls (VR Settings):
- Choose H-pattern or sequential I-pattern, with reversible push/pull shifts.
- Optional no-grip flick shifting: Single Both or Double Down. Adjust direction,
  axis, active zone, movement/wrist deadzones and flick speed.
- Engine rumble follows the hand into the shifter zone. Shift clicks, optional
  sound, green zone LEDs, red upshift and blue downshift flashes are configurable.
- LED and PAGE > buttons below the stereo cycle ambient color and the monitor's
  video/music or live RPM, acceleration and brake graphs.
- Wheel Deadzone adjusts the tracked steering wheel's center dead travel.
- Grab Dash Screen defaults to On. Hold Grip on the screen to move/tilt it.
  While holding, right stick up/down changes size; left lowers opacity and
  right raises it (5-100%). Changes save when you release the screen.
  Reset Screen restores the fitted placement, size, opacity and grabbing defaults.
  A previously saved Grab Dash Screen: Off setting is retained.
- No-grip shifting gestures default to Off. These controls still need a headset
  playtest for feel and final visual placement.
