# Release notes

## 0.6.1-dev · Starting-line cockpit view

You now see the approach to the starting line from the driver's seat as your car rolls onto the grid. You stay in the cockpit throughout the countdown. At **GO**, the game uses your saved racing camera. If you normally drive in the cockpit, you stay there as the race begins.

This works with both automatic and manual transmission on **Windows PCVR** and **Meta Quest 3 standalone**.

### Downloads
- **Windows PCVR** — Extract the [PCVR ZIP](https://github.com/DR-89/nfsu2-vr/releases/download/dev-2026-11-06-r3/NFSU2VR-PCVR-dev-2026-11-06-r3.zip) into a writable folder, keep your existing `game/` and `Music/` folders, and run `Start-PCVR.cmd`.
- **Quest 3** — Install the [Quest APK](https://github.com/DR-89/nfsu2-vr/releases/download/dev-2026-11-06-r3/NFSU2VR-Quest3-dev-2026-11-06-r3.apk) over your existing installation. Version code 9 uses the same signing certificate as 0.6.0-dev.

Both builds require your own original Xbox USA / NTSC-U game data. The camera transition was checked in an automated PCVR test with both transmissions and with cockpit and external driving views. An in-headset check of the final Quest build is still pending.

---

## 0.6.0-dev · Custom radio, cockpit video and interior lighting

### Radio and video
- Put MP3, FLAC, PCM WAV or H.264 MP4 files in `Music` and restart the game. On Windows the folder is beside `nfsu2-pcvr.exe`. On Quest it is `Android/data/org.nfsu2.quest/files/Music` in shared storage. During driving and replays, the radio shuffles the files together. MP4 videos can have AAC audio or play silently.
- The larger 16:9 screen above the radio plays video and shows a 16-band spectrum for audio-only tracks and EA TRAX. The screen and touch controls sit deeper in the center console for more clearance around the shifter.
- **VR Settings → Radio Soundtrack** offers **Auto**, **Custom Only** and **EA TRAX Only**. Auto keeps EA TRAX in menus. Custom Only also silences the original menu music when you have playable custom files. An empty or unreadable library falls back to EA TRAX.

### Cockpit, mirrors and minimap
- The darker cabin has LED strips around the instruments, dashboard, doors, shifter and rear shelf. Pick one of eight colors. **Pulse to Music** switches between reactive and steady lighting. **Interior LED Strips** turns the strips off. The music drives both the spectrum and LED pulse at every radio volume, including mute.
- Each car recalls its own mirror angles. In a replay, click the **right stick** to cycle through five replay cameras and the driver's seat. In the driver's seat, hold **Grip** on a mirror to adjust it as the replay runs.
- Grab the minimap with either hand to move it. Tilt or turn your wrist for up to 45° of pitch and yaw. Release **Grip** to save the placement. Existing placements carry over. The five minimap adjustment rows have been removed from VR Settings.

### Downloads
- **Windows x64 PCVR** — Extract the [PCVR ZIP](https://github.com/DR-89/nfsu2-vr/releases/download/dev-2026-11-06-r2/NFSU2VR-PCVR-dev-2026-11-06-r2.zip) in a writable folder. Keep your existing `game/` and `Music/` folders. Launch `Start-PCVR.cmd`, or `Start-PCVR.cmd -SteamVR` for Steam Link.
- **Meta Quest 3 standalone** — Install the [Quest APK](https://github.com/DR-89/nfsu2-vr/releases/download/dev-2026-11-06-r2/NFSU2VR-Quest3-dev-2026-11-06-r2.apk) over 0.5.0-dev to keep imported data and saves. This is version code 8, signed with the same certificate as 0.5.0-dev.

Both builds require your own original Xbox USA / NTSC-U game data. Matching port and FFmpeg sources, build information and SHA-256 checksums are attached.

**Verification.** All 25 Windows test executables, 32 Android importer tests and 13 Python input tests passed. Media tests covered MP3, FLAC, WAV, H.264 video with and without audio, and spectrum and LED response at full, low and muted volume. The current PCVR executable reached a focused OpenXR session. Standalone Quest headset testing of this release is still pending.

---

## 0.5.0-dev · RPM-based engine rumble, replay cameras and cockpit controls

Development prerelease for Windows x64 PCVR and standalone Meta Quest 3.

### RPM-based engine rumble
- **Engine REV Rumble** uses each car’s actual RPM, idle speed and rev limit. Feel a light vibration at idle that builds as the revs rise. More throttle makes it stronger at the same RPM. When the revs drop after an upshift, the rumble eases.
- Turn it on in **VR Settings → Advanced Haptics / Rumble → Engine REV Rumble**. It starts off by default. With **Grip Feedback** on, releasing the wheel with one hand stops engine vibration on that side. Road and collision feedback still reach both hands.

### Replays
- Press **Grip** on either VR controller to cycle through five original replay views and the driver's seat. Your choice stays active until the next press or the end of the replay, even with the timeline hidden. You can still look around and use the cockpit radio from the driver's seat.
- Tap **B / Circle** to hide or show the timeline. A long press keeps its original replay action. On runtimes that report hand-tracking sources, curling a bare finger no longer triggers replay controls.
- Fixed premature returns to the results screen. Playback also restores the recorded VR gear changes and pedal inputs for the race.

### Cockpit
- **Minimap and world map.** The minimap is round and its rim turns blue within reach. Hold **Grip** to move it, then release to save. Placing it in front of the steering wheel now draws it in front. In career free roam, reach for the rim and press **A** on the right controller or **X** on the left to open the world map.
- **Seat and radio.** Tap the highlighted controls with your virtual index finger. The door armrest moves the seat in 1 cm steps. EA TRAX supports fingertip volume adjustment, previous, play/pause and next during driving and cockpit replays. Fixed track skipping and a case where the radio showed a title but stayed silent.
- **Shifter and race display.** A dot on the H-pattern shows where your hand is in the gate. Set the guide to **Always**, **Near Shifter** or **Off**. Lap count and times now sit on the right-side window.

### Settings
- **Mirror Resolution** offers five presets from **256×64** to **4096×1024**. The default is **1024×256**. Choose a lower resolution to reduce rendering work on standalone Quest.
- **Master Gain** adjusts overall game audio from **−12 to +12 dB**.

### Download and update
- **Quest 3** — Install the [Quest APK](https://github.com/DR-89/nfsu2-vr/releases/download/dev-2026-11-06/NFSU2VR-Quest3-dev-2026-11-06.apk) over the previous installation to keep your imported data and saves. Version code **7** uses the same signing certificate as 0.4.0-dev.
- **Windows PCVR** — Extract the [PCVR ZIP](https://github.com/DR-89/nfsu2-vr/releases/download/dev-2026-11-06/NFSU2VR-PCVR-dev-2026-11-06.zip) to a writable folder, keep your existing `game/` directory, and run `Start-PCVR.cmd`. For Steam Link, use `Start-PCVR.cmd -SteamVR`.

Both versions require your own original Xbox USA / NTSC-U game data. The release includes the matching port and FFmpeg sources, build information and SHA-256 checksums.

**Verification.** All 24 Windows regression executables, 32 Android importer tests and 13 Python input tests passed. A captured PCVR replay exercised the Grip camera cycle across all six views and 8,400 camera updates. The signed Quest APK was checked for package identity and upgrade compatibility. A fresh in-headset run of these final packages is still outstanding.

---

## Cockpit controls and cutscene playback · 0.4.0-dev

Development prerelease for standalone Meta Quest 3 and Windows x64 PCVR.

- **Manual shifting.** Grab the gear lever with either hand. On a five-speed, reverse sits below fifth. On a six-speed, reverse has its own lane to the right of sixth. For a standing start, you can grab the lever during the countdown and push it fully forward in the left lane. The car stays in neutral at the signal if you leave the lever in neutral.
- **Steering.** Release both grips to let the wheel return to center. Catch it with either hand to stop it at its current angle. Pause and press **X** to open **VR Settings → Wheel Return Speed**. The default is 240°/s and the slider ranges from Off to 540°/s.
- **Mirrors and pedals.** Grip the inside mirror to aim it, or grip either outside mirror to adjust both together. The reflections respond to head movement. Each car remembers its mirror angles, and the next race restores the last camera. Use **Reset Car Mirrors** in VR Settings to restore the default aim. Accelerator and brake travel follow trigger pressure, and the clutch pedal moves when shifting.
- **Cockpit radio.** A control highlights when it is within grip range. Drag the volume slider or press previous, play/pause or next. The track title and artist appear below the buttons. Muting and raising the volume keeps the current song playing. The radio controls also recover after closing VR Settings.
- **Steam Link.** Hold **Y** on the left controller for about 0.65 seconds while driving to open the in-game pause menu.
- **Intro and story videos.** Both platforms now use a native VP6 decoder. In a PC comparison across 2,700 frames, average decode time fell from 7.23 ms to 1.13 ms with identical output. On a Quest 3, decoding averaged 1.19 ms per opening-video frame and 1.52 ms for the first story video over 360 frames of each clip. These timings measure decoding. Headset presentation and streaming can still introduce judder.

### Downloads and installation

**Quest 3** — Install the APK over your existing installation to keep imported data, settings and saves. The package is `org.nfsu2.quest`, version code 6, signed with the same certificate as 0.3.2-dev.

**Windows PCVR** — Extract the entire ZIP to a writable folder and keep your existing `game/` directory when updating. Run `Start-PCVR.cmd` for the active OpenXR runtime. For Steam Link, connect the headset to SteamVR and run `Start-PCVR.cmd -SteamVR` to select SteamVR explicitly.

Both platforms require your own supported original Xbox USA / NTSC-U game data. The release also includes the exact port and FFmpeg source archives, `BUILD-INFO.json` and `SHA256SUMS.txt`.

The standing-start tests exercised both hands, five- and six-speed gearboxes, the starting brake and neutral at the signal. The updated PCVR game started with SteamVR tracking active. An in-headset hand test of the countdown grab is still outstanding.

---

## VR settings and vehicle cockpit fixes - dev-2026-10-17

Development prerelease for **Meta Quest 3 standalone** and **Windows x64 PCVR**.

## October 18 update - steering hotfix (0.3.1-dev)

- Small tracked wheel turns now compensate for the original game's **24% stick
  deadzone**. At the default 360-degree total rotation, this removes roughly
  43 degrees of dead travel in each direction.
- The change applies to hand-driven steering only. Normal stick/gamepad
  deadzones are preserved; a new wheel grab uses the stick's effective steering
  so transferring control does not add a steering jump.
- Wheel animation and held glove contacts continue to follow the physical
  wheel angle. Existing rotation, sensitivity and saved settings are retained.
- Windows and Quest ARM64 wheel/settings tests pass, including **132,066**
  range/sensitivity samples, all **65,536** stick values and small one-/two-hand
  turns. The Windows SDL gamepad regression also passes. Native Quest driving
  confirmed fine steering response and successful stick takeover.
- Both release binaries are updated. The Quest APK is **0.3.1-dev / version
  code 4**, signed with the same certificate. PCVR headset acceptance of this
  hotfix remains outstanding. The download filenames/URLs remain unchanged.

## Changes since October 15

- **VR Settings:** pause during a race and press **X**. Adjust steering
  sensitivity (50-300%), total wheel rotation (180-1080 degrees), seat height
  (+/-35 cm), seat distance (+/-40 cm) and per-eye render resolution. Settings
  persist; resolution changes apply after restarting. The entry is restricted
  to the root pause menu, not race notifications or other screens.
- **Vehicle fitting:** individual base seat/cabin profiles for all 29 stock
  cars, including the Miata's shorter cabin and removal of the rear bench in
  the Miata and 350Z. The shared cabin is fitted to each body; it is still a
  common generated interior rather than 29 individually recreated interiors.
- **Body and mirrors:** fix the missing cockpit body on cars such as the
  Lancer Evo VIII; remove crossed original driver-proxy cards causing black
  obstructions. Original center/side mirrors remain recognized after changing
  the seat and include the lower mirror glass on several vehicles.
- **Clear cockpit windows:** remove the player's glass/tint only in Cockpit
  View, keeping the original body, window frames, mirrors and exterior tint.
- **Instruments:** place the speed display on the dashboard and the map on the
  center console. Driving notifications no longer move the gauge back to its
  original HUD position; pause menus remain complete.
- **Controls:** right stick click cycles cameras; left click holds rear view
  outside the cockpit. Both clicks recenter. Right stick left reads SMS, right
  enters a race/shop, and up/down shifts in Manual transmission. Return the
  stick to center between actions. A is nitro; B is handbrake.
- **Gamepads:** mapped SDL USB/Bluetooth controllers use the same driving
  layout, with supported rumble. Motion-controller wheel grips take priority
  while held. Physical controller combinations need broader hardware testing.
- **Cockpit overhead:** cache mirror geometry, upload immutable cabin/glove
  geometry to device-local memory, avoid redundant HUD foveation allocations
  and defer pipeline-cache disk writes until outside races.
- **Release assets:** restore the verified textured racing gloves after a local
  diagnostic dump overwrote the asset. Packaging now checks the glove format,
  checksum and equality of the APK and source cockpit assets.
- **PCVR startup:** retry transient OpenXR initialization/session failures and
  provide reconnect/retry dialogs instead of continuing with a black console
  and audio only. This recovery path still needs broader headset testing.

## Downloads and updating

- **NFSU2VR-Quest3-dev-2026-10-17.apk**: standalone Quest APK, **0.3.1-dev / version
  code 4**. Install over the previous APK to retain imported data and saves;
  the signing certificate is unchanged. Do not uninstall first.
- **NFSU2VR-PCVR-dev-2026-10-17.zip**: extract all files, including `cockpits/`,
  and launch **Start-PCVR.cmd**. When updating, retain your existing `game/`
  directory and its saves. Connect Virtual Desktop/VDXR before launching.
- Matching source ZIP, **BUILD-INFO.json** and **SHA256SUMS.txt** identify the
  exact source, dependencies, validation and binary checksums. The release tag
  also points to this source revision.

Supply your own supported **original Xbox USA / NTSC-U** disc. Quest imports
ISO/XISO, ZIP or 7z; PCVR imports ISO/XISO, so unpack compressed archives first.
No original game content or saves are included. Generated cockpit/glove assets
and the required application runtime libraries are bundled.

## Validation and limits

For the October 17 base release, both builds completed and seventeen Windows
regression executables, 32 Android importer unit tests and 13 Python input tests
passed. The targeted October 18 checks are listed above. Shared cockpit tests cover
all 29 vehicle profiles, 2,068 stock submeshes, center/side mirror selection,
seat offsets, pause/notification states and 6,417 captured draw records. The
native Quest cockpit suite passed on the headset before October 17 packaging.
That release restored the previously released textured glove asset. The October
18 update changes only steering input and release metadata; rendering and
cockpit/glove assets are unchanged. Private
retail test fixtures are excluded from all downloads.

Native Quest user testing confirmed the Lancer body/driver correction, all
three mirrors after seat changes, Miata seating and rear-bench removal, the
dashboard gauge, console map and cockpit-only tint removal. This is not a
visual playthrough of every car, tuning combination or seat position.

**This remains an experimental development build. Stable 72 FPS is not yet
established across gameplay.** The latest cockpit changes and startup recovery
still need separate PCVR headset acceptance. Other headsets/OpenXR runtimes
and physical gamepad combinations are not comprehensively verified.

## Links and credits

- Repository: https://github.com/DR-89/nfsu2-vr
- Gameplay video: https://youtu.be/MHHi2G6Lixk
- Support development: https://www.patreon.com/c/OpenSourceDevDirk
- Based on antoxa2584x: https://github.com/antoxa2584x/nfsu2-sw
- Recompilation toolkit: https://github.com/sp00nznet/xboxrecomp

Upstream credits and license notices are retained. This unofficial project
is not affiliated with EA, Meta or Microsoft.
