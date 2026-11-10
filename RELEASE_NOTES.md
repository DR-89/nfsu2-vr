# Release notes

## 0.7.2-dev · Restore XR gameplay pacing up to 120 FPS

Replacement for the withdrawn **0.7.1-dev / dev-2026-11-10-r2** release.
Windows PCVR and Quest standalone are rebuilt from the same source revision.

### Cause and correction

The 0.7.1 PCVR preset stopped requesting a fixed 72 Hz display rate. The game
also used that refresh-rate option to decide whether to bypass its retail
60 Hz presentation wait. Keeping the runtime's rate therefore disabled the
XR timing path and restored the cap. This coupling was missed by the earlier
helper-only timing tests.

XR race pacing now depends on XR being active and the game being in a race,
independently of any refresh-rate request. Both the immediate presentation
path and the elapsed-time simulation path remain enabled at automatic,
72, 80, 90 and 120 Hz. Frontend/intro timing and the guest vblank clock remain
unchanged. This correction applies to both platforms.

For **120 FPS on PCVR**, select **120 Hz in Virtual Desktop** before launch.
The shipped `NFSU2_VR_REFRESH_HZ=0` preserves that rate. Sustained rendered FPS
still depends on scene, PC performance and runtime behavior; the final build
has not undergone a sustained 120 FPS gameplay measurement.

All 0.7.1 control, camera, lowercase keyboard, shifter pickup, menu scrollbar,
screen-opacity and default screen-grab changes are retained.

### Downloads and validation

- **Existing PCVR installation:** extract `NFSU2VR-PCVR-Update-0.7.2-dev.zip`, close the game, run **Update-PCVR.cmd** and choose the folder containing `nfsu2-pcvr.exe`. Replaced files are backed up; game data, saves, music and preferences are preserved.
- **Full PCVR package:** extract the PCVR ZIP and run **Start-PCVR.cmd**.
- **Quest:** install the new APK over the existing app. Package `org.nfsu2.quest`, version code **12**, retains the previous signing certificate.

The new regression harness compiles the actual guest presentation hook with
a synthetic runtime. The withdrawn source fails with the shipped preset;
the fix passes eight option combinations, command payload preservation,
XR disabled, and repeated menu/race transitions. Simulation checks pass at
72/80/90/120 Hz on Windows and directly on Quest 3 ARM64. Nine additional
Windows control/camera/cockpit regression executables pass.

Builds, package inventories, signatures and checksums are verified. Exact
checks are recorded in BUILD-INFO.json. This remains a development prerelease;
the existing unsupported CopChase limitation remains. No retail game data,
saves or optional community asset packs are included.

---

## 0.7.1-dev · Withdrawn: 60 FPS regression

**Replaced by 0.7.2-dev above.** Historical control and camera changes below are retained in the replacement.

Development update for **Windows PCVR** (including Quest 3 through Virtual Desktop / VDXR) and **Meta Quest 3 standalone**. Both builds contain the control, camera, screen and menu corrections below. The Quest APK was added to this same release with version code **11** and the same signing certificate as 0.7.0-dev. Its matching source is supplied in the separate Quest source archive; the original PCVR tag and archive remain unchanged.

### Controls and cockpit interaction

- Right-stick up/down now send career/context actions instead of shifting gears. Up challenges a rival or enters an event/shop; down opens career status. Left reads SMS and right retains the event/shop shortcut. Return the stick to center between actions. Manual shifting remains available through X/Y or the physical lever.
- Short vertical stick movements and D-pad taps remain active for at least 100 ms so the guest input poll can see them. Menus consume a single digital direction from either stick. Driving controls no longer depend on successful cockpit rendering, and a previous screen grab cannot keep blocking menu navigation.
- The I-pattern lever can only be picked up within 6.5 cm of the knob. Its full travel remains available while held. Radio, LED, page and seat controls take priority over optional no-grip shifting gestures.
- Names, rooms and keyphrases support lowercase input and display their actual case. The keyboard starts in lowercase; X toggles case, and holding Y still reveals a keyphrase.
- **Grab Dash Screen defaults to On.** Hold the screen with Grip: right-stick left lowers opacity and right raises it (5-100%); up/down continues to resize it. Opacity, size and placement save on release. Previously saved Off settings are retained; enable the option once or use Reset Screen to apply its new default.

### Menus and camera recovery

- Cockpit Controls and Community Mods display a proportional scrollbar, direction arrows and explicit counts such as **10 MORE BELOW**. Scroll with stick/D-pad up/down. Explanatory text stays clear of the scroll indicator.
- Camera restore, cockpit cycling and rear-camera ownership now respect the configured 64/128 MB memory window. The old 64 MB checks rejected valid camera objects after session changes. Regression checks cover repeated sessions and allocations in the expanded window.

### PCVR performance settings

- The preset keeps the display rate selected in Virtual Desktop / OpenXR instead of requesting 72 Hz. `NFSU2_VR_REFRESH_HZ=0` retains the runtime rate; an explicit supported rate can still be selected in `pcvr.env`.
- Mirror tracing skips off-texture samples while preserving its sample grid, hit refinement and silhouette validation. Repeated environment lookups for the vinyl diagnostic switch are cached.
- **Stable 90 FPS has not been confirmed.** Both 0.6.1-dev and 0.7.0-dev requested 72 Hz in their presets, so that setting alone does not establish the reported regression's cause. A matched Quest 3 / VDXR comparison and in-headset confirmation of call answering and session transitions remain pending. Standalone Quest control and visual acceptance also require an in-headset test.

### Download and validation

The 0.7.1 downloads have been withdrawn. Use the 0.7.2 packages above.

The Windows build and focused input, profile keyboard, shifter, cockpit interaction, camera lifetime, mirror, radio and frame-timing checks passed. Screen tests cover both hands and all cars, independent size/opacity axes, deadzones, opacity limits and saving opacity-only edits. Menu rendering was inspected at the top, middle and bottom of the lists. Both mirror shader variants compile, and 200,000 numerical ray cases retain every visible reference sample. Exact checks and hashes are recorded in BUILD-INFO.json and SHA256SUMS.txt.

The Quest ARM64 runtime/game was rebuilt from the current shared code. APK identity is `org.nfsu2.quest`, version code 11 / 0.7.1-dev; signature verification matches 0.7.0-dev. Its packaged game library matches the freshly stripped build, and the APK contains only the six intended notices/cockpit assets plus four ARM64 libraries. The unchanged Java importer/WebSocket suite remains green (37 tests, cached Gradle results). No headset was connected for a new standalone playtest.

This remains a development prerelease. CopChase remains unsupported and disabled by default; its known game-integration failure is not addressed here. Original game content, saves, credentials and optional community asset packs are excluded. Supply your own Xbox USA / NTSC-U game data. Matching port and FFmpeg source archives are included.

---

## 0.7.0-dev · Cockpit controls, telemetry and mirror fixes

Development prerelease for **Windows PCVR** and **Meta Quest 3 standalone**.

### Cockpit controls

- Choose **H-pattern** or sequential **I-pattern** in **VR Settings → Cockpit Controls**, with reversible push/pull direction and adjustable travel. Use Manual transmission in the game.
- Optional **no-grip gestures** work inside the shifter zone. Choose single flicks in both directions or a double flick to downshift; adjust direction, axis, zone, movement/wrist deadzones and speed. Gestures default to Off.
- Engine rumble continues when your hand enters the shifter zone. Shifts can produce a haptic click, an optional sound and LED cues: green for the active zone, red for upshift, blue for downshift.
- Press **LED** below the stereo to cycle ambient colors. Press **PAGE >** to switch directly between video/visualizer and live RPM, acceleration and braking graphs without opening the VR menu.
- **Wheel Deadzone** adjusts the tracked wheel's center dead travel. Enable **Grab Dash Screen** to move and tilt the monitor; right-stick up/down while holding it changes its size. Screen opacity and reset are in the same settings.

### Mirror and screen corrections

- Remove the old 2D rear-view mask that caused a black bar in the physical mirrors, with guarded texture identification and reflection-depth checks.
- Adjusting the center mirror no longer changes the outside mirrors' source camera.
- Mirrors show the road and traffic. The player's own interior and body are deliberately excluded from the rear capture.
- A grabbed monitor placed in front of the wheel now renders in front of it. Depth still hides the monitor when it is behind the wheel, including at partial opacity.

### Experimental online and community support

The snapshot also includes **Online Profiles**, protected local credentials, native Free Roam cars, and race browsing/joining/hosting for the nfsu2.xyz service. Profiles remain separate from career saves; creating one does not reserve a service name. Community settings include GPS distance, stance, headlights, career options and optional asset-pack support. See the [integration status](https://github.com/DR-89/nfsu2-vr/blob/dev-2026-11-10/docs/nfsu2-xyz-integration.md) for implementation evidence and remaining checks.

Live browser-player interoperability and full headset acceptance remain experimental. **CopChase is not a supported feature of this release**: it defaults to Off, its assets are excluded, and its experimental game-integration regression currently fails. Optional community artwork/audio packs are not bundled.

### Downloads and updating

- **Windows PCVR:** extract the [PCVR ZIP](https://github.com/DR-89/nfsu2-vr/releases/download/dev-2026-11-10/NFSU2VR-PCVR-dev-2026-11-10.zip) into a writable folder, preserve your `game/` and `Music/` folders, and run **Start-PCVR.cmd**. For Steam Link, use **Start-PCVR.cmd -SteamVR**.
- **Quest 3:** install the [Quest APK](https://github.com/DR-89/nfsu2-vr/releases/download/dev-2026-11-10/NFSU2VR-Quest3-dev-2026-11-10.apk) over your existing installation. Version code **10** retains the **0.6.1-dev signing certificate**.

Both require your own original **Xbox USA / NTSC-U game data**. No original game content or saves are included. Matching port/FFmpeg source archives, build information and SHA-256 checksums accompany the binaries.

Automated build, regression and Vulkan depth checks are recorded in **BUILD-INFO.json**. The PCVR build has started successfully with tracked OpenXR presentation; final in-headset acceptance of every change on both platforms is still pending. This remains a development build, with no guarantee of stable frame rates or complete compatibility.

---

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
