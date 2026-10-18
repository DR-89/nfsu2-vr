# VR settings and vehicle cockpit fixes - dev-2026-10-17

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
