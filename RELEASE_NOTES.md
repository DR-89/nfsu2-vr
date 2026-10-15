# Cockpit and motion steering — dev-2026-10-15

Development prerelease for **Meta Quest 3 standalone** and **Windows x64 PCVR**.

## Changes

- A shared anthracite/blue cockpit, including doors, front seats and rear bench,
  fitted inside the original car body. The extra headliner and duplicate pillars
  are removed; the original body and hood remain visible.
- Cockpit View in the regular camera cycle, an animated steering wheel and
  translucent Tripo racing gloves. Grip anywhere on the rim to steer with one
  or both hands, including hand-over-hand turns.
- 360-degree total steering travel (180 degrees either way). Releasing the wheel
  retains steering; deliberate left-stick movement restores stick steering.
- Held gloves keep their wheel contact through tracking recovery, diagonal hub
  crossings and steering stops. Release and regrip to choose a new contact point.
- Functional interior and side mirror surfaces, using three sectors of one
  live rear camera. Smaller speed display behind the wheel and lower map.
- Controller vibration from game rumble; enable Vibration in the game settings.
- Left stick click changes cameras; right stick click looks back outside the
  cockpit. Both clicks recenter. While driving, A is nitro, B is handbrake and
  right stick up/down shifts in Manual transmission mode.
- Pause menus remain one complete panel rather than moving their lower corners
  onto the map and speed display.

## Downloads and updating

- **NFSU2VR-Quest3-dev-2026-10-15.apk**: standalone Quest 3 APK, version 0.2-dev
  (version code 2). Install as an update to retain imported data and saves.
- **NFSU2VR-PCVR-dev-2026-10-15.zip**: extract the entire folder, retaining
  `cockpits/`, then launch `Start-PCVR.cmd`. Keep your existing `game/` and saves.
- **NFSU2VR-Source-<commit>.zip**, **BUILD-INFO.json** and **SHA256SUMS.txt**:
  matching source, validation details and checksums. This release tag points to
  the matching source commit; GitHub's automatic source downloads match it too.

No original game content is included. Supply your own supported **Xbox USA /
NTSC-U** disc. Quest imports ISO/XISO, ZIP or 7z; PCVR imports ISO/XISO (unpack
compressed archives first). The generated cockpit/glove assets are bundled.

## Validation and remaining work

The recent cockpit fit, camera controls, mirrors, rumble, instruments, pause
menu and stable hand contact were confirmed on native Quest. Windows and Quest
regression tests cover the shared code, and both platform builds complete.
Exact release checks and dependency pins are recorded in BUILD-INFO.json.

This remains an experimental development build. Stable 72 FPS is **not yet
achieved across gameplay**. Vehicle-specific cockpit fitting is incomplete,
and the new PCVR cockpit features have not received a separate headset test.
PCVR development uses Virtual Desktop/VDXR; other runtimes remain unverified.

Based on [antoxa2584x/nfsu2-sw](https://github.com/antoxa2584x/nfsu2-sw) and
[xboxrecomp](https://github.com/sp00nznet/xboxrecomp). Existing upstream credits
and license notices are retained.
