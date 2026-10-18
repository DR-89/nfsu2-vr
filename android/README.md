# Quest 3 development port

This branch builds the NTSC-U Xbox executable as native Android ARM64 code and
runs it standalone on Quest 3. It is based on `antoxa2584x/nfsu2-sw`, revision
`6e19726414e829d808f7acb0258c0902f04a923a`.

Current performance target: stable 72 newly rendered game frames/s, including
dense scenes and crashes, without new rendering regressions at the accepted
image quality. This supersedes the earlier 120 Hz target and has not been met
across the game yet.

## VR settings and controller update (2026-10-17, testing)

Pause during a race and press **X** on Touch or a mapped gamepad to open the
English **VR Settings** panel. A visible `[X] VR SETTINGS` entry appears below
the pause menu. Use either stick or the D-pad to select a row and adjust it
left/right. **A** selects Reset Defaults or Save and Back; **B** saves and
returns to the game's pause menu without also activating it.
The entry is authorized only by the active root `UI_Pause.fng` package;
race notifications, loading screens, options and confirmation dialogs cannot
open it. Holding X before pausing requires release and a new press.

- **Steering sensitivity:** 50–300%. Higher values reach full game steering
  with less physical wheel movement.
- **Wheel rotation:** 180–1080 degrees total, lock to lock; default 360.
  This controls physical travel independently of steering sensitivity.
- **Seat height:** ±35 cm, in 1 cm steps. Positive moves the eye upward.
- **Seat distance:** ±40 cm, in 1 cm steps. Positive moves the eye backward.
  The interior, instruments and grasp points stay attached to the car.
- **Render resolution:** Default, 1280×1344, 1680×1760, 2550×2640,
  2940×3080 or 3360×3520 per eye. The latter four match the community
  resolution selector. **Save and restart the game** to apply a resolution.
  Unsupported sizes fall back to the runtime/default size and are logged.
  Higher resolutions require considerably more GPU time and memory.

Settings persist in `vr-settings.ini` in the app's private files directory on
Quest, or `%APPDATA%\OpenSourceDevDirk\NFSU2VR` on Windows. Game data and saved
profiles are separate. Changing the wheel ratio resets the current grab;
release Grip and grasp the rim again.

Mapped SDL USB/Bluetooth gamepads feed the same driving controls as Touch.
The active input device is selected by use; a held VR wheel has priority.
Left stick steers, triggers brake/accelerate, A is nitro, B is handbrake,
right stick click cycles cameras, and left stick click holds rear view only
outside the cockpit. Start pauses. Right stick up/down shifts with manual
transmission; **left reads SMS and right enters a race/shop**, one action per
sideways deflection. Return to center between actions. Menus retain their
normal navigation and A/B behavior. Gamepad rumble follows the game effects
when the controller supports it; enable the game's Vibration option.

This revision also caches original mirror geometry, uploads immutable cabin
and glove geometry to device-local GPU memory, and defers pipeline-cache disk
writes until outside races. Player-shell backface culling is disabled only for
the detected seated player's body. Mirror detection accepts additional heights
and slants beyond the Peugeot reference; the three mirrors still share sectors
of one rear camera. These are targeted changes, not a claim of stable 72 FPS.

Seat adjustments now move the shared XR eye transform while the game's base
camera and original mirror geometry remain fixed to the car. This avoids
losing mirror recognition when changing height or distance. A Pontiac capture
also isolated crossed retail driver-proxy cards directly around the seated
eye. Only that complete two-card geometry is hidden in the player's cockpit
world pass; exterior views, other cars and the original shell are retained.
The follow-up Lancer capture showed the same cards just outside the original
seat-position window. Recognition now uses their complete topology and shape,
independent of driver-side or seat height. The decoded stock geometry of all
29 cars passes: exactly 29 driver proxies are removed among 2,068 submeshes,
with all other 2,039 retained, on Windows and Quest. This includes A3/Skyline
right-side drivers and the higher Navigator seat. The Lancer and Pontiac live
captures each match exactly one proxy; the existing mirror fixtures still pass.
The user confirmed the Lancer obstruction is gone and its body and outside
mirrors remain visible. A separate center-mirror issue was then isolated:
its glass extends 3 mm below the former height cutoff. The bounded planar
glass search now includes the lower 240SX, Eclipse, Lancer, Miata, RX7 and TT
mirrors. All 29 center mirrors are found across the 2,068 stock submeshes in
Windows and Quest tests; the previously working 23 selections are unchanged.
This checks geometry selection, not a visual playthrough of all vehicles.
The user confirmed all three Lancer mirrors work on Quest, including after
changing Seat Height or Seat Distance.

Transient `Chyron_IG` notifications accompanying a race HUD keep the cockpit
instrument layout. They no longer trigger the complete menu plane (which
temporarily returned the gauge to its original screen position). Actual pause
menus, options, dialogs and unknown packages remain unsplit. The mono cockpit
HUD also skips the unused foveation attachment, preventing repeated rate-map
allocation when switching between HUD and eye-image resolutions.

The Lancer Evo VIII capture identified a second retail body exclusion inside
`CarRenderInfo::Render`: the highest-detail car is omitted when the main
camera, raised by 0.5 m, is inside its bounding box. This is independent of
the camera's hidden-car pointer. The cockpit now bypasses only that call's
positive result for the current player's own render-info bounds. The original
test still runs; other cars, views, callers and non-cockpit cameras retain
their results. Car dimensions and collision data are not modified.

`tools/audit-cockpit-cars.py game build/car-audit` prepares private fixtures
from local stock geometry and extracts the actual lifted routine and wrapper
for `tests/quest_car_body_test.c`. With this USA disc it covers 29 vehicle types
and 569 decoded stock solids. All 812 comparisons pass on Windows x64 and
Quest ARM64, including unrelated calls and register/x87 preservation. Eight
stock-bound aggregates reproduce the fixed-seat exclusion, including Lancer,
Golf and several SUVs; the Peugeot does not. This is an automated regression
of the body-hide decision, not a visual playthrough of all cars or all tuning
parts. Generated fixtures remain private under `build/`.
For the separate geometry regression, run
`tools/audit-cockpit-driver-cards.py game build/car-audit/stock-solids.json build/driver-audit`.
Pass `build/driver-audit/stock-meshes.bin` as the fifth fixture argument to
`quest_cockpit_test` (after the cabin, mirror, UI and live geometry fixtures).
The extractor checks vertex/index ranges and independent per-submesh bounds;
its expected driver labels do not use the runtime classifier being tested.
The same extraction writes `stock-center-mirrors.bin`; pass this as the sixth
fixture argument to test all stock center-mirror selections and negative
body/trim cases. Its labels are stored separately from the runtime detector.

Validation: Windows and Quest tests pass for settings, bounds, save-format
roundtrip, menu input consumption, variable wheel range/sensitivity, stable
hand contact, and horizontal action pulses. Synthetic mirror variants and the
42-batch Peugeot capture at four seat positions pass. The SDL virtual gamepad
test passes button, trigger, axis, deadzone and detach checks on Windows.
Windows and Quest also pass 17,253 stereo seat/head-pose configurations and
the private 49-batch Pontiac fixture, which matches exactly one driver proxy.
The live notification-list fixture fails with the previous menu classifier
and passes with the fix, alongside captured pause/resume states and synthetic
notification-plus-menu cases on both Windows and Quest.
Other-car visuals, physical gamepads, menu placement, music-change stutters and
PCVR reconnection still require headset testing.

## Shared cockpit prototype (2026-10-15)

In a race, use the normal camera-change control to cycle into Cockpit View.
It is an additional entry after the four usual driving camera slots, with no separate
button or long press. Both stick clicks recenter.
One anthracite/blue cabin currently serves all cars. Dashboard, both door
interiors, front seats, rear bench and floor come from one Tripo
model, with existing trim refitted for VR. The rim and hub rotate with steering
input. The race HUD is positioned above the dashboard. Menus and race-intro
cameras retain their original behavior. Native geometry is bundled in the APK;
no additional import of cockpit files is required.

The wheel turns up to 180 degrees left or right (360 degrees lock to lock). The current eye is at
forward/left/up `(-0.10, 0.35, 0.95)` m: 8 cm closer to the wheel and 10 cm
lower than the previous fit. The cabin remains fixed relative to the car;
the cabin asset and dashboard panels shift oppositely in eye coordinates.
The wheel center is approximately 33 cm forward and 21 cm below the eye.
The shared Tripo headliner and its attached roof trim are omitted (parts
1, 3, 84, 90 and 96). The original
player body and hood remain visible in cockpit view. The Tripo cabin's
duplicate window pillars and upper frames are omitted so the original car
supplies those parts. The user confirmed the lower/closer seat and roof removal.

Cockpit 3D uses near-plane clipping instead of the retail depth clamp. The user
confirmed that this removed the black occluder, but the 50-cm near plane also
cut away the upper car shell when tilting the head. The cockpit world now uses
an 8-cm near plane, with matching depth-offset conversion for car surfaces,
decals and particles. The user confirmed the revised fit and retained upper edge
with head tilt on 2026-10-15.
`NFSU2_VR_COCKPIT_CLIP=0` restores clamping for comparison. Other driving
cameras retain the 0.5-unit projection; the 2D UI keeps its existing path.

## Touch vibration

The original Xbox rumble reports feed both Touch controllers. Both Xbox
motor channels contribute to intensity; they represent heavy/light effects,
not left/right hands. A continuous strength curve keeps small signals gentle
while preserving full-strength impacts. Actual road/collision coverage comes
from the game's effects; enable **Vibration** in the game options.

Output runs on the OpenXR thread using finite 100-ms pulses, renewed at most
every 50 ms unless strength changes. A 65-ms peak hold preserves collisions
between rendered frames. Unchanged USB reports refresh feedback; 250 ms
without reports expires it. Focus loss clears pending effects and stops the
controllers, so they do not replay an old collision on return. `RECOMP_RUMBLE=0`
disables XR vibration; `NFSU2_VR_HAPTICS_TRACE=1` and `RECOMP_RUMBLE_TRACE=1`
temporarily log output and guest motor levels. Disable tracing for performance
measurements. Synthetic timing, intensity and focus tests pass on Quest and
Windows. The user confirmed working vibration on native Quest on 2026-10-15;
detailed strength calibration and road coverage across tracks remain unverified.
The implementation uses [OpenXR haptic output](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrApplyHapticFeedback.html).

## Grip steering and ghost hands

In Cockpit View, bring a controller near the steering rim and hold its Grip
button. One or both hands can steer by moving around the wheel. Releasing both
retains the last steering angle, including the actual input sent to the game.
Free hands do not move the wheel; grabbing again at any rim point resumes from
that retained angle without a jump. To return to left-stick steering, release
both grips and deflect the stick at least 25% sideways. Ordinary stick steering,
including centering on release, then remains active until the next wheel grab.
Small stick drift preserves the held angle; either grasped hand takes priority.
Regrabbing uses the current stick's effective steering after the game's deadzone,
not an older guest input packet. Tracked wheel input compensates for the original
game's 24% stick deadzone, so small wheel turns reach the car proportionally.
Physical stick/gamepad input keeps its normal deadzone; compensation does not
change the displayed wheel angle or the gloves' contact points.
Repeated hand/stick transfers, one-hand priority, drift, centering and regrabs
are covered by the native grip test. Triggers still control acceleration/braking.
By default the wheel permits 180 degrees of travel in each direction (360 degrees
total); VR Settings can change rotation and sensitivity. Adding or releasing
one hand keeps the current angle. Diagonal movement through the hub or an abrupt
pose jump rebases angular tracking without releasing the grip or rotating the
wheel by the discontinuity. Missing poses freeze that hand's contribution while
its Grip remains held; the other tracked hand can continue steering. Grasped
gloves remain attached to the rim during pose gaps. A confirmed Grip release
works even without a pose; unavailable button samples get a bounded 500-ms grace
period. Focus loss, recentering, leaving the cockpit or longer button loss
requires releasing Grip before grabbing again. Acquisition uses 65% squeeze,
release below 15%, so relaxing the pressure slightly does not drop the wheel.

Translucent blue/dark racing gloves generated by Tripo are visible only in
Cockpit View. Free hands follow controller grip poses; grasped hands follow
their individual acquisition points on the rotating rim, including crossed
hands. A held glove keeps that wheel-local contact through tracking recovery,
hub crossings, unequal two-hand movement and steering stops; those events
must not reposition it. Release and regrip select a new point anywhere on the
rim. There are no fixed left/right sockets. Adding a hand at any angle preserves steering.
The corrected wrists point toward the driver while fingers wrap the rim.
Each glove has 5,980 triangles and shares a 1024-square texture. Two baked
poses use immutable GPU geometry and add at most two multiview draws.
Controller poses use core OpenXR
[action spaces](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrCreateActionSpace.html)
at the eyes' predicted display time. This is controller-driven hand animation,
not optical finger tracking. The cabin/seat fitting is unchanged.

`tests/quest_wheel_grip_test.c` checks single/two-hand angles at 30 through
120 FPS, grip transitions, wraparound, lock return, tracking loss, controls and
mesh validity on Quest and Windows. Both builds pass; the hand/rim fit has also
been inspected in a 3D preview. The user confirmed the button mapping and
one/two-hand steering, then reported reversed hands. The corrected orientation,
Tripo glove appearance and crossed-hand contact fit still need headset acceptance.
`NFSU2_VR_GRIP_TRACE=1` logs grip, squeeze and tracking changes; 0 or an absent
setting disables tracing. Regression tests reproduce diagonal hub crossings,
tracking interruptions, single-hand recovery with two grips, real releases
without a pose and missing-button timeouts across clock wrap.
On 2026-10-15 the user confirmed that the grip stays held during diagonal
movements and that steering and switching back to the left stick work on Quest.
The later hand-only jumping report reproduced in a regression test: updating
the visual contact every frame relocated the glove despite retaining grip and
steering. The contact is now assigned only on acquisition. Tests remove the
rendered wheel transform and compare the full glove pose before/after tracking
gaps, hub crossings and steering stops at 30/60/72/90/120 Hz, with one or both
hands. On 2026-10-15 the user confirmed on native Quest that the hands remain
stable while held and releasing/regripping at another rim point works.
Accepted APK: `build/nfsu2-quest-grip-contact-20261015.apk`.

The asset, controls, camera math and steering rotation have passed native unit
tests on Quest and Windows. On 2026-10-15 the user confirmed the replacement
full cabin, normal camera cycle and steering-wheel animation together in a
native Quest race. Vulkan validation was disabled for this test. An earlier
sample of the previous prototype measured 49.6 FPS (p50 19.98 ms, p95 23.03 ms, maximum
29.60 ms over 120 frames); this is not a stable 72 FPS result or a controlled
comparison of cockpit overhead. Seat fitting across every car remains untested.
See [cockpit asset details](../assets/cockpits/README.md) for source provenance,
preparation and the path toward separate interiors for different cars.

## Verified on the headset (2026-10-13)

- The APK installs and boots the original game using Vulkan on Adreno 740.
- The user reached and drove a race with Touch controls.
- OpenXR instance/session, separate eye swapchains, tracked view poses and
  Touch actions work. The three-dot startup failure was an undersized
  swapchain-format array, fixed by querying the runtime's count first.
- A cinema screen presents the intro. The user confirmed picture and buttons.
- Experimental stereo renders independent color/depth targets for each eye.
  Captured eye images differ; the user confirmed depth and head movement.
- The user confirmed readable menus and race HUD, with smoother racing after
  grouping eye draws and sharing reflection probes. Correcting projection
  scales removed the flying environment geometry.
- A 960x720-per-eye test was visibly sharper, but the user explicitly reported
  that clarity remains inadequate and that some scenes still stutter.
- At 1680x1760 per eye, the user confirmed sufficient sharpness and subjectively
  similar smoothness. Occasional lag and fragments remain; the port is not done.
- Full guest-memory alias and kernel-memory tests passed on the Quest. These
  include atomic replacement of Android reservations and all 28 mirror windows.
- Camera projection tests pass on the Quest. Thirteen host input checks pass.
- The tracked CPU frustum restored the missing start-line cars and car parts,
  confirmed by the user. Displaced environment fragments and head-dependent
  reflection errors were subsequently reported; their correction is under test.
- All 12 installed retail files were verified against local SHA-256 hashes.

**This remains an experimental port.** The first performance pass measured
roughly 27–30 FPS in several tracked race sections, with drops in dense scenes.
This is not a stable VR frame rate or evidence of 120 newly rendered FPS.
Keeping light-buffer fades outside the HUD transform and snapshotting sampled
render-target feedback removed the reported light/shadow artifacts in a headset
test. A later test still showed intermittent scene fragments; investigation
continues. Do not describe a successful
compile or working intro as a finished, comfortable VR port. Game-side culling,
screen effects, pause menus, audio continuity, saves and long-session stability
still need validation.

## Build on Windows

Requires Git, CMake, Python, Android SDK platform/build-tools 35, NDK 29,
JDK 17+ and Gradle 8.11.1. Dependencies are pinned in `dependencies.json`;
the scripts validate their Git revisions and keep them under `build/deps`.
Nothing is installed globally.

```powershell
py -3.13 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r tools/quest-requirements.txt
.\.venv\Scripts\python.exe tools/inspect_game.py .\game
.\.venv\Scripts\python.exe tools/regen.py .\game --gen-dir .\gen
.\tools\build-quest.ps1 -Sdk 'C:\path\to\android-sdk' `
  -Ndk 'C:\path\to\android-sdk\ndk\29.0.14206865' `
  -JavaHome 'C:\path\to\jdk' -Gradle 'C:\path\to\gradle.bat'
```

The disc root in `game/` must contain `default.xbe` and `NFSUNDER/`. Generation
requires an empty output directory and runs XBE analysis, disassembly, function
identification, ABI analysis and native C generation. The successful retail run
produced 19,536 translated functions plus three manual entries, with no failed
translations. Unresolved guest stubs still exist; booting does not validate them.

Output: `android/app/build/outputs/apk/debug/app-debug.apk`. This development APK
uses a local debug signature and packages ARM64 game/runtime libraries, SDL,
OpenXR loader and the shared C++ runtime. Retail data remains separate.
`build-quest-runtime.ps1` builds only the runtime and regression executables;
`build-quest.ps1` also links the generated game and packages the APK.

## Install

### Import game data on the Quest

The APK now opens a 2D setup panel when no supported game data is installed.
Select **Select archive**, choose a local ZIP, 7z or Xbox ISO through the Android file picker,
wait for verification/import, then press **Start game**. Subsequent launches
through the normal game entry start VR directly. The separate launcher entry
**NFSU2 · Game data** always opens setup for a new import.

Use the **unmodified Xbox USA / NTSC-U disc** in any of these forms:

- An unencrypted **ZIP or 7z** containing `default.xbe` and the complete
  `NFSUNDER` directory (`ZDIR.BIN`, `ZZDATA0.BIN` through `ZZDATA9.BIN`).
- A **ZIP or 7z containing one Xbox ISO/XISO**. The disc is unpacked and its
  XDVDFS game partition is read automatically; no PC conversion is needed.
- A raw Xbox **ISO/XISO**, including supported full-disc/video-partition layouts.

A common enclosing folder and different filename casing are accepted. The
European Xbox disc, PC game and encrypted archives are unsupported. LZMA/LZMA2
7z decoding is bounded to 128 MiB; a 64 MiB dictionary, as used by the user's
original disc archive, fits this limit. Very large dictionaries are rejected
with instructions to repack. The APK contains no retail assets.

To package the already extracted, verified disc from this workspace:

```powershell
.\.venv\Scripts\python.exe tools/package-quest-data.py .\game .\build\NFSU2-Xbox-USA-Quest.zip
adb -s YOUR_QUEST_SERIAL push build/NFSU2-Xbox-USA-Quest.zip /sdcard/Download/NFSU2-Xbox-USA-Quest.zip
```

You can instead copy an existing ZIP, 7z or Xbox ISO to the Quest's `Download`
folder over USB. Allow about **5 GB free in addition to an archive of extracted
files**, or about **17 GB extra for the original 7z containing a full disc**.
The latter stages the source archive, the 7.8 GB disc image and the verified
2.4 GB game files. Existing data stays in place until replacement is ready.
The source archive is never deleted automatically; it is no longer needed
after import. Disc decompression can take several minutes. Progress and cancel
remain available, and a partial wake lock lets the worker continue if the
headset display sleeps (leaving/closing the setup can still cancel the import).

The importer checks exact sizes and SHA-256 hashes of all 12 supported retail
files. Archive paths, duplicate names, CRCs, file bounds and XDVDFS directories
are also validated. ZIP/7z inputs and total uncompressed archive contents are
capped at 12 GiB. It stages the new files, preserves existing non-retail content including
`game/UDATA` and `game/TDATA`, then swaps directories with rollback/recovery on
the next launch. Saves bundled in an imported archive are ignored. APK updates
preserve saves; uninstalling the app deletes its private files. Import and native
game access share an exclusive process lock. Close the running game before
reimporting; a busy setup panel offers **Check again** and a return to the game.

Pure Java regression tests cover ZIP, LZMA/LZMA2 7z, nested disc imports,
partition layouts, cancellation, corruption, locking and recovery using synthetic
files, without retail assets:

```powershell
gradle -p android testDebugUnitTest
```

The importer uses Apache Commons Compress 1.28.0 and XZ for Java 1.12.
Their notices and those of transitive dependencies are included in the APK
asset `importer-third-party-notices.txt`. The opt-in `RetailImportSmoke` Java
test can additionally validate a private real archive into a new output directory.
It is never included in the APK or run by the synthetic test suite.

Import validation on 2026-10-14: all 32 synthetic tests passed. The user's
original 5.9 GB 7z (containing a 7.8 GB Xbox ISO) imported successfully both on
the host and through the Quest file picker. All 12 retail files passed their
SHA-256 checks; the six saved-game/configuration files were byte-identical
before and after reimport. The user confirmed the English panel's placement
and working progress display. A concurrent game launch returned to setup
without interrupting import. After completion, the normal launcher started
the native game and OpenXR delivered frames. Temporary device staging files
were removed and the import wake lock was released.

### Developer installation over ADB

```powershell
.\.venv\Scripts\python.exe tools/install-quest.py `
  --serial YOUR_QUEST_SERIAL --adb 'C:\platform-tools\adb.exe' --game .\game --start
```

The installer stages data in `/data/local/tmp/nfsu2-assets`, copies it with
`run-as` into the app's internal storage, and validates every required file before
starting. Copying directly into Android's external data directory is unreliable
on this Quest firmware. Runtime files live under
`/data/user/0/org.nfsu2.quest/files`; saves use `game/UDATA` through the Xbox runtime.
APK updates preserve these files. Uninstalling removes the app's private data.

For code-only updates (no asset recopy):

```powershell
adb -s YOUR_QUEST_SERIAL push android/app/build/outputs/apk/debug/app-debug.apk /data/local/tmp/nfsu2-quest.apk
adb -s YOUR_QUEST_SERIAL shell pm install -r /data/local/tmp/nfsu2-quest.apk
adb -s YOUR_QUEST_SERIAL shell am start -n org.nfsu2.quest/.QuestActivity
```

The headset must be awake. Android can create the process while delaying the
SDL native entry point until its surface resumes; the old log then remains until
boot actually begins. Regular Android screenshots do not capture this VR layer.

## Controls and diagnostics

- While driving: Touch A = nitro, B = handbrake; right stick up = shift up,
  down = shift down. Return the stick to center before another shift.
  Select manual transmission in the game to use manual shifting.
- Menus retain A to confirm, B to go back and right-stick D-pad navigation.
  X/Y retain their original actions (also secondary gear buttons in gameplay).
- Left menu button maps to Start; right stick also supplies the menu D-pad.
- Left stick maps to the Xbox left stick; triggers map to the Xbox triggers.
- Right stick click cycles cameras, including leaving Cockpit View (on release).
- Left stick click holds rear view in the original driving cameras. It has
  no rear-view action in Cockpit View; turn your head there.
- Grips only grasp the steering wheel in Cockpit View; they do not send
  camera or rear-view buttons in any view.
- Press both sticks together to recenter the seated origin.

The stick-click actions map to the retail **analog White/Black buttons**,
not the Xbox thumb-click bits. This fixes being trapped in the cockpit when
the old grip-to-White camera shortcut was suppressed for wheel steering.
The camera pulse is preserved while holding the wheel; only rear view is
suppressed inside the cockpit. Packet and full camera-cycle regression tests
cover both views, recenter chords and 25–125 Hz input polling.
On 2026-10-15 the user confirmed the corrected click mapping, cockpit exit
while gripping, rear-view restriction and recentering on Quest. The device log
also records repeated complete camera cycles and a cockpit exit with a hand
still gripping the wheel.

The driving mapping follows the [original Xbox manual](https://oldgamesdownload.com/wp-content/uploads/manuals/need-for-speed-underground-2_xbox_manual_en_5rt.pdf):
retail A/B are handbrake/nitrous, X/Y are shift up/down. OpenXR swaps only A/B
while an unpaused driving camera is active. The right axis produces bounded
100-ms X/Y pulses with separate activation/release thresholds, and its analog
throttle/brake and D-pad outputs are consumed in gameplay. Menu/focus transitions
reset the shift latch and require neutral before the next gear command. Packet
tests cover held input, entry from menus, axis noise, timers wrapping and
25–125 Hz polling. Gameplay state is published from the guest thread.

The cockpit mirrors use the game's separate rear camera (retail view 3).
The Vulkan backend stores its 256x64 logical target at 512x128 and maps the
horizontally flipped rear image onto the **original car's glass triangles**.
There is no additional rectangular mirror below the original interior mirror.
The outside mirrors use left/right sectors of this same wide rear image; these
are not three independently positioned reflection cameras. Glass detection
requires the player's rigid car transform and small, thin glass geometry.
The recorded test car identifies 2 interior and 10 triangles per outside mirror;
all 42 nearby recorded batches pass the positive/negative geometry checks on
Windows and Quest. Other car models still need visual coverage. After the seat
was lowered, a stale forward-seat constant rejected all three glass surfaces.
The detector now shares the camera's seat definition, and tests glass bounds
relative to the car origin rather than the eye. All 42 recorded batches are
checked at four seat positions, including the current fit. On 2026-10-15 the
user confirmed all three mirrors and the new A/B/right-stick driving mapping
working on the installed native Quest build.
Only rear targets and glass drawn in the current frame are eligible. The
camera-manager hook creates the retail rear-camera mover on cockpit entry if
needed, and releases only its own mover on exit. Saved mirror options and
look-back controls are unchanged. The retail 2D mirror quad is omitted in the
cockpit. Native captures show rear scenery in the original glass; the user accepted
the combined mirror and instrument layout on 2026-10-15.

Cockpit instruments use a separate transparent 1280x960 HUD target, rendered
once for both eyes with the original 640x480 coordinates and scissor. The map
sits lower left (center -0.30, -0.21, -0.38 m). The speed/tachometer plane is
centered behind the wheel (0, -0.205, -0.56 m), with a 0.24 m square source
region. It is drawn transparently over the fixed dummy instruments, before
the moving wheel and hands, so the cabin cannot cut away digits and the wheel
still covers them naturally. No added black panel or dashboard cutout remains.
The rest of the race information retains its previous windshield placement.
In the cockpit, pausing switches this same UI image to one complete windshield
plane, including both lower corners. Pause submenus use the same complete plane;
the map/gauge split resumes only with active gameplay. The mode is recorded once
with the UI image and shared by both eyes. The guest reads the active frontend
package list, not the input gate (which stays enabled in the pause menu) or the
loaded package cache (which can retain closed menus). Active non-HUD packages
use the complete plane, including options and confirmation dialogs, except
the verified `Chyron_IG.fng` driving notification alongside an active HUD.
The complete menu is drawn after the
cabin and hands without depth occlusion. Other camera views retain their existing
HUD path. Native Windows and Quest tests check all four menu corners and the
continuous panel diagonal. Private snapshots of the active pause and resumed
race package lists cover the state detector. On 2026-10-15 the user confirmed
that the installed native Quest pause menu, including its lower corners, is
displayed correctly. APK: `build/nfsu2-quest-pause-menu-state-20261015.apk`.
Pipeline cache
bit 5 marks separate alpha coverage for this HUD target only; normal title
blending is unchanged. Both final eye captures show the speed digits behind the wheel and the map
lower left, without the added black border. The user accepted this final placement on 2026-10-15.

Optional `files/quest-env.txt` accepts `NFSU2_` and `RECOMP_` environment settings:

```text
NFSU2_VR_STEREO=0
```

This selects the cinema fallback. Stereo defaults on. `NFSU2_VR=0` disables
OpenXR for development, but the APK manifest still identifies an immersive app.
`NFSU2_VR_UNITS_PER_METER` adjusts tracking translation scale (default 1).
`RECOMP_GL_SCALE=1.5` uses 960x720 race targets. Alternatively,
`NFSU2_VR_RESOLUTION=1` (the Quest default) uses the runtime's recommended eye extent for the race
and scales its half-resolution effects proportionally. Values from 0.25 to 1.5
are accepted; 0.75 currently means 1260x1320 on this headset, and 1 means
1680x1760. The user judged the latter sharp enough, but performance still varies.
Small cube probes retain their independent size. The front end's 1280x480
horizontal-AA target now renders at 1920x1440 and is presented through a separate
4:3 OpenXR quad swapchain, avoiding the former resample through the stereo eye
image. Its stationary menu screen is 4.8x3.6 metres at 3 metres distance (formerly
3.6x2.7): one third larger in each dimension. Movie/cinema fallback frames retain
the previous physical size. Runtime swapchain limits cap the menu image extent.
The race resolution and HUD size are unaffected. Low-resolution original font
and artwork textures can still limit sharpness; this does not replace those
assets. The user confirmed the larger menu is comfortably readable.
Increasing resolution has a substantial GPU cost.
`NFSU2_VR_FOVEATION=1` enables experimental fixed foveated shading when the GPU
supports `VK_KHR_fragment_shading_rate`. The central ellipse (65% of each eye's
width and height) keeps one shader invocation per pixel; the outer world uses
2x2 shading. HUD and post-processing retain full shading. It is off by default
and has not yet been visually validated on the headset.
`RECOMP_VK_DRAW_STATS=1` enables renderer statistics without the executor's
expensive vertex diagnostics enabled by `RECOMP_DRAW_STATS`.
`RECOMP_GL_DUMP=frames/frame,300` captures every 300th frame, including a
`-right.bmp` image in stereo. Disable dumps for performance measurements and to
avoid accumulating files. `NFSU2_VR_CAPTURE=1` records up to 32 distinct guest
vertex programs with their constants for local camera diagnosis.
With that option enabled, writing a snapshot stride (for example `100`) into
`files/frames/xr-trace.request` captures exactly one subsequent tracked stereo frame's draw
metadata and intermediate images. It consumes the request, writes
`xr-draws.csv`, at most 40 `xr-step-*.bmp` images and the final left/right images
`xr-trace-final.bmp` / `xr-trace-right.bmp`. `xr-trace-world.bmp` and
`xr-trace-world-right.bmp` capture both eyes immediately before the added
cockpit, separating retail surfaces from the interior when diagnosing blocked
vision. `xr-trace-mirror.bmp` saves the current rear-camera image when available.
`xr-trace-hud.bmp` saves the separate cockpit HUD. `xr-near-geometry.bin`
contains nearby triangles for the requested diagnostic frame only.
These readbacks run only for a requested trace. The CSV includes depth-buffer address,
depth comparison, stencil/cull enable and polygon-offset registers. Recognized
post-processing passes also save `xr-before-blur.bmp`, `xr-before-lights.bmp`
and `xr-before-grade.bmp` when present in that frame. An absent pass does not
produce a new file; do not treat files left by an earlier capture as current.
An optional second number selects the first draw to capture (`1 1900` captures
the next 40 draws after draw 1900). A stride of `100000` captures only the final
image. Requests remain pending while tracking is unavailable or in cinema mode.
Readbacks stall rendering; use only for diagnosis, not FPS measurements.

```powershell
adb -s YOUR_QUEST_SERIAL shell run-as org.nfsu2.quest cat files/nfsu2.log
.\.venv\Scripts\python.exe -m unittest discover -s tests -p 'test_quest_input.py' -v
```

The CMake build also produces `quest_culling_test`, `quest_shader_test`,
`quest_projection_test`, `memory_layout_posix_test`
and `kernel_memory_posix_test`. Push them to a dedicated `/data/local/tmp` directory,
make them executable and run them there. Memory tests require matching SDL,
OpenXR loader and C++ shared libraries in `LD_LIBRARY_PATH`, plus the toolkit's
synthetic `xboxrecomp/tools/conformance/test.xbe` for the layout test.

## Implementation and limits

`quest_xr.c` owns Android loader initialization, Vulkan integration, session state,
frame timing, tracking, swapchains and Touch actions. `nv2a_vk.c` duplicates render
targets by eye and applies tracked eye projection to verified NTSC-U perspective
programs. The verified 320x240 road-reflection geometry also receives the eye
projection. Captured shaders use `c[96..99]` as the model-view-projection matrix;
`quest_projection.h` checks the microcode and prefers the explicit projection
rows at `c[113..115]`, which remain valid for non-uniformly scaled models.
Rigid/uniform model matrices also have a tested scale-recovery fallback.
For verified shaders, Vulkan captures the original clip position immediately
before `RCC oPos.w`, retaining the remaining guest lighting/texture calculations.
This avoids reconstructing positions from the already divided/scaled Xbox
screen coordinates. Unsupported programs retain the previous reconstruction.
GL shader output is unchanged. It does not convert a completed flat picture to 3D.
The eye projection now uses the captured game's 0.5-unit near plane. The former
0.05-unit plane reduced D24 precision tenfold. The shader also preserves the
difference between the guest's actual clip Z and its expected projected Z;
otherwise depth-only offsets for layered surfaces disappear during reprojection.
Projection invariants, including signed depth offsets, pass on the Quest, but the
user reported that this did not improve the remaining dark flicker. Black pixels
and intermittently missing car parts were also reported, apparently in both eyes.
Core Vulkan validation found no draw-time errors in the collected race log;
this does not cover shader correctness or synchronization validation. Its
temporary Android GPU debug settings were restored after the diagnostic run.
Captured race and intro draws had polygon offset disabled, so the missing
polygon-offset implementation does not explain those particular captures.

The NTSC-U camera builder `sub_000A33F0` now replaces the main view (ID 1)
CPU culling planes after the original matrices have been built. Auxiliary views
4/6 are included only when they reference the primary camera. Render matrices
remain intact for the vertex reprojection path. The culling volume conservatively
encloses both tracked eye frusta, with 35% tangent margin and 0.25 metre padding
for pose latency. Both Y and Z are reversed from XR up/back to the retail
camera's down/forward convention; a pitched-camera regression covers this.
A mutex protects the render-to-game-thread snapshot; tracking
older than 500 ms falls back to the retail planes. `quest_culling_test` passes
on Quest for asymmetric eyes, yaw/pitch, translated game cameras and rejection
of points behind/outside the view. The user confirmed that the previously missing
start-line cars and car parts are now fully visible. Briefly displaced environment
fragments remain under investigation. `NFSU2_VR_CULL=0` restores the original planes; the temporary
`NFSU2_VR_CULL_VIEW` override selects a camera ID for diagnosis.
`NFSU2_VR_CULL_CAPTURE=1` writes bounded camera/frustum snapshots in `files/frames`.

In stereo, `sub_000A6530` skips the retail camera-lens rain overlay, whose 32x32
flat backbuffer copies produced large square drops. The separate world-rain
pass remains active. `NFSU2_VR_LENS_RAIN=1` restores the original overlay for
comparison; cinema and non-VR retain it automatically. In-game confirmation
of this adaptation is pending.

The road shader's projected reflection coordinates previously came from the
flat Xbox viewport even when its surface was rendered in VR. Both the reflection
geometry and its sampling now use the same eye projection. Only the captured
road-program instruction pattern with a 320x240 stage-1 render target enables
the UV override; cube-map directions and unrelated texture stages are preserved.
Shader generation, camera projection and culling tests pass on the Quest.
Visual confirmation of the reflection correction remains pending.

The front end's observed 2x-AA target stays on the cinema screen. The race uses
640x480 targets, with per-eye post-process input selection. This classification
is title-specific and still needs coverage of all menus and race modes. The
game's original visibility/streaming decisions remain a limit for large head
turns. The experimental HUD path is not yet a fully separate compositor layer.
The renderer snapshots a target before a draw that also samples it, avoiding
an unsupported sampled-image attachment feedback loop. Reflection cube faces
are shared between eyes; their cameras are independent of headset tracking.
HUD clipping is evaluated on the original 2D plane before presenting its result
in VR. This keeps off-screen atlas content clipped and moves the minimap's
clipping rectangle with the map. The captured 256x64 rear-view camera is shared
between eyes. Outside the cockpit its display quad uses the HUD plane; the
cockpit maps it onto the original mirror glass as described above.
Full-screen triangles and quads stay in eye space, including untextured alpha
updates that still carry a stale texture address in the guest registers.
Optional OpenXR CPU/GPU performance hints and Android thread classifications
were accepted by the device. `RECOMP_VK_INSTANCE=1` enables experimental stereo
instancing. Deferred draws compare complete register blocks, original vertex
identities and epochs, eye/frame identity and explicit projection constants.
Only verified perspective programs are eligible. A measured run combined
3,723 batches into 2,538 draw calls per frame; that is not an equivalent FPS gain.
Instancing remains disabled by default on Quest. A later performance test enabled
it together with Vulkan state caching and caused disappearing environment and
incorrect menus. A later isolated state-cache test with instancing OFF reproduced
the missing menu and badly corrupted environment. State caching is therefore
sufficient to cause this regression; instancing has not been cleared by this
result. Both remain disabled. Do not treat either faulty run's reduced draw
count or GPU time as a valid speedup: missing geometry can make rendering cheaper.

## Performance work (2026-10-13)

The pre-change tracked driving/crash log (`reports/quest-performance-baseline.log`)
contains 16–24 FPS sections and 109–123 ms frames with about 8,000 draw calls,
without material shader compilation or texture-upload cost. A 25-second simpleperf
sample attributes about 63% of sampled application CPU time to the pushbuffer/
renderer thread, 19% to the main game thread, and 6% to the APU thread. These are
CPU sample shares, not frame-time percentages; the route is not a deterministic
benchmark.

- OpenXR now releases images after queue submission, without waiting for the
  current GPU fence on the CPU. All Vulkan/OpenXR queue access stays on the same
  thread. The existing two frame-slot fences still guard command-pool reset,
  staging memory reuse and resource destruction. `NFSU2_VR_GPU_WAIT=1` restores
  the synchronous path for comparison. This follows the
  [Khronos image-release guidance](https://github.com/KhronosGroup/OpenXR-Docs/issues/177).
- Right-eye snapshots use a bounded, reusable CPU arena instead of thousands
  of per-draw malloc/free pairs. It resets only after right-eye draws, including
  held instances, have copied their inputs into the fenced Vulkan ring. Its
  alignment, growth, pointer lifetime, reset and capacity test passes on Quest.
- Experimental Vulkan state caching reuses identical vertex-input layouts and unchanged sampled-image
  descriptors. Command-buffer generation invalidates both caches, including
  after a mid-frame ring flush. It is OFF by default and in the device settings
  after the visual regression above. The isolated `RECOMP_VK_STATE_CACHE=1` test
  also failed; do not re-enable this path without a specific fix. Vertex-layout
  reuse and partial image-descriptor updates have not been isolated from each other.
- GPU timestamp queries now measure elapsed GPU commands separately from CPU
  fence waits. Logs also report XR begin/end time, mid-frame flushes and tracked
  race frame-time p50/p95/max over 120 frames. GPU elapsed intervals and CPU
  work overlap and must not be added together. Snapshot timing excludes the
  right-eye replay triggered by its capacity limit.

The first hardware pass (asynchronous submission + arena, instancing still off)
held 1680x1760 pixels per eye. Several race intervals measured 26.7–30.0 FPS,
GPU elapsed 28–34 ms and CPU fence waits near zero. Dense ~7,000-draw frames still
reached about 50–60 ms. This is an encouraging non-identical-route comparison,
not a controlled speedup percentage. The user confirmed smoother play with
unchanged image quality after this first pass. This is the active configuration:
asynchronous submission and arena ON, instancing and state caching OFF. The
second pass's regression log is preserved as
`reports/quest-performance2-regression.log`. After restoration, a tracked race
again measured 30.0 FPS (p95 about 34.1 ms), with 3,115 draws/calls per frame,
zero instances and zero state-cache reuse (`reports/quest-performance-restored.log`).
The user confirmed that menus and environment returned to their previous
appearance after restoration. The confirmed first performance improvement
remains active; the additional caching/instancing experiment remains disabled.

The next pass removes the retail 30 FPS limit (`NFSU2_VR_60FPS=1`, Quest default).
The original flip queue still runs against the real 59.94 Hz vblank clock, with
its fences and late-flip flag intact; only requested intervals above one vblank
are reduced to one. The simulation's requested minimum step is also capped at
1/60 second, preserving its original elapsed-time quantization and maximum step.
The packed-command preservation and simulation-minimum checks pass on Quest.
`NFSU2_VR_60FPS=0` restores both original behaviors.

The first unlocked race log (`reports/quest-60fps-baseline.log`) contains frequent
35–48 FPS intervals and a peak 120-frame interval of 50.3 FPS. Across its 47
tracked intervals the median interval rate is 38.2 FPS, with a minimum of 18.9;
this includes race-start stalls and is not a steady-state or route-matched
benchmark. Five-second simulation-time intervals closely follow wall time during
driving. The user confirmed normal game speed and an unchanged image. Race
resolution remains 1680x1760 per eye, with instancing and state caching off.

A separate fixed-foveation experiment (`NFSU2_VR_FOVEATION=1`) was accepted by
the Quest Vulkan driver. Its 210x220 shading-rate map covers the same 1680x1760
race image: a central ellipse spanning 65% of each dimension keeps 1x1 shading,
while outer world fragments use 2x2. HUD and full-screen passes retain 1x1;
menu and reflection targets do not get the map. The attachment and dynamic
combiner follow the [Khronos shading-rate interface](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdSetFragmentShadingRateKHR.html).
The initial race capture (`reports/quest-60fps-foveation.log`) does not establish
a substantial speedup; scene complexity still dominates. The user confirmed
correct rendering and acceptable peripheral sharpness. It remains enabled in
the device's development configuration; it is opt-in, not an APK default.

The subsequent isolated state-cache test enabled `RECOMP_VK_STATE_CACHE=1`
with `RECOMP_VK_INSTANCE=0`, keeping the confirmed timing unlock, resolution
and foveation configuration. The user reported a missing menu and a badly
corrupted environment. The failed run is preserved in
`reports/quest-60fps-cache-only-regression.log`; its FPS figures are invalid as
a speedup comparison. State caching was disabled immediately and the app
restarted. Runtime counters again report zero vertex-layout and image-descriptor
reuse. The restored device configuration retains the 60 FPS unlock, fixed
foveation, 1680x1760 race images and the confirmed larger 1920x1440 menu.

### Full descriptor updates and stereo upload work

The next CPU profile attributed about 9.4% of sampled app CPU time to Vulkan's
push-descriptor call and its callees, with the renderer thread accounting for
about 63%. `NFSU2_VR_DESCRIPTOR_TEMPLATE=1` now uses one pre-created Vulkan
descriptor-update template for the same three uniform buffers and four sampled
images. Every draw still writes all seven bindings and emits its vertex input
state. This does not re-enable partial state caching or instancing. Unsupported
template creation falls back to the original full update path.

The template build compiled and ran on Quest. Core Vulkan validation covered
intro, menu and dense race draws without a descriptor/draw error. It did report
an instance-extension dependency warning/error in the OpenXR instance creation
path (`VK_KHR_surface_protected_capabilities` missing
`VK_KHR_get_surface_capabilities2`); this is separate from descriptor submission
and remains unresolved. Validation was then disabled and its prior Android
settings restored before measuring performance. The user confirmed correct
rendering and smoother play. Logs: `reports/quest-descriptor-template.log`,
`reports/performance-template.data`, and `reports/quest-template-validation.log`.
The route was not deterministic, so CPU sample shares and interval FPS are not
a controlled speedup percentage. Dense ~7,000-draw frames still reached 50–60 ms
and sometimes exhausted the 48 MB upload ring.

The following experiment shares one draw's immutable uploaded vertex/index
bytes between its two eyes. It retains the CPU snapshot as a fallback, all
per-eye uniforms and all Vulkan state-setting calls. A ring-buffer or
command-buffer-generation change rejects the old reference. It never shares
between different objects or frames, and is disabled when instancing or the
known-broken state cache is enabled. `NFSU2_VR_GEOMETRY_REUSE=0` is the original
path, `1` reuses uploads, `2` compares freshly generated right-eye data against
left-eye uploads while rendering normally, and `3` verifies 100,000 tracked-race
draws before enabling reuse at a frame boundary. A mismatch disables the
experiment. Checks cover vertex layouts, relative offsets, stored/converted
attributes, constant attributes and indices, excluding unused alignment bytes.
On Quest, mode 3 checked 101,130 tracked-race draws without a byte mismatch,
then enabled reuse. The user confirmed correct rendering and weaker drops in
dense scenes and crashes. Both the descriptor template and geometry reuse
remain enabled in the device configuration; future launches use mode 1 directly.
They remain opt-in in the APK. Resolution stays at 1680x1760 per race eye and
1920x1440 for the larger menu; instancing, state caching and validation remain OFF.

After reuse became active, `reports/quest-geometry-reuse.log` recorded 34 tracked
120-frame windows: 19.7–52.1 FPS, with a median interval rate of 36.1 FPS, and
0.84–3.31 MB of geometry upload avoided per frame. This is a non-identical-route
measurement, not a controlled speedup percentage. Dense scenes still cause
substantial drops; stable 60 FPS has not been achieved. The matching CPU profile
is saved in `reports/performance-geometry.data` and
`reports/performance-geometry-symbols.txt`.

### Work toward 72 FPS

`NFSU2_VR_72FPS=1` requests 72 Hz through the optional OpenXR
`XR_FB_display_refresh_rate` extension after checking the runtime's supported
rates. The device confirmed the change from 90 to 72 Hz. In the retail race
timing branch, flips use interval zero with the immediate flag; framebuffer
payload, the flip queue, fences and the 59.94 Hz guest vblank remain intact.
Frontend timing retains the previous path. The race simulation uses elapsed
time without a per-frame minimum, keeping the retail accumulator, quantizer
and maximum step. An initial 1/72 minimum sped up jittering 72 FPS sections by
about 2%; removing that floor restored measured five-second race intervals to
wall time. Packed-command and jitter regression checks pass on Quest. The user
confirmed normal game speed and correct graphics. This permits 72 FPS but does
not guarantee that CPU/GPU work finishes within 13.89 ms.

`NFSU2_VR_UNIFORM_REUSE=1` shares the immutable guest vertex-program constants
between the two eyes of the same snapshotted draw. The headset transform still
has its own per-eye upload. It requires the geometry record and shares its
ring/command-buffer lifetime checks, with instancing and state caching off.
Modes 2 and 3 compare fresh uploads; mode 3 switches to reuse only after
100,000 tracked-race comparisons. On Quest, 102,344 race draws matched with
zero differences before reuse became active. `NFSU2_VR_LOAD_CLEAR=1` uses
attachment load clears only for a complete attachment rectangle at the start
of a render pass. Partial clears and clears in an existing pass keep their
original commands; depth and stencil load operations remain independent.
The user confirmed correct images and weaker drops with both changes.

`NFSU2_VR_GPU_PROFILE=1` adds bounded timestamp pairs around existing render
passes, collected after the frame-slot fence without a CPU query wait. It is
off for ordinary comparisons. The diagnostic run attributed most GPU time to
the main image (about 19–22 ms in sampled dense sections), with reflections
around 4 ms. About 40 passes and four genuine feedback copies occur per frame;
unused texture stages did not account for those copies, so no speculative
texture-stage skipping was retained. Log: `reports/quest-72-gpu-profile.log`.

`NFSU2_VR_SINGLE_SHADER=1`, with `RECOMP_VK_INSTANCE=0`, compiles a Vulkan-only
vertex-shader variant with 192 constants and a constant instance base of zero.
The default shader still supports 16 instances, and GL source is unchanged.
The source hash separates SPIR-V variants. The user confirmed correct rendering
and smoother play. Geometry, uniform reuse and full descriptor updates remain
compatible. Logs: `reports/quest-72-uploads.log` and
`reports/quest-72-single-shader.log`. Simple sections reached roughly 72 real
game FPS, but dense sections still fell near 20–40 FPS. Later low-complexity
or stationary sections dominate whole-log medians; these are not controlled
speedup percentages or proof of a 72 FPS minimum. An occasional 250 ms retail
flip-retirement timeout was also observed in the 72 FPS runs and remains open.

A further opt-in instancing comparison requires both `RECOMP_VK_INSTANCE=1`
and `NFSU2_VR_CHECKED_INSTANCES=1`. In addition to the previous state/epoch
checks, every candidate compares its vertex bytes against the first draw's
immutable Vulkan upload and checks all constant attributes. A difference
rejects the merge. Command-buffer generation still guards the upload lifetime.
State caching stays off; single-instance shaders and cross-eye upload reuse
are inactive during this comparison. The user confirmed correct rendering but
roughly unchanged smoothness. Instancing was therefore disabled again, and the
confirmed single-instance shader and cross-eye geometry/uniform reuse settings
were restored. Log: `reports/quest-72-checked-instances.log`.

Stable 72 FPS across dense scenes and crashes is the minimum requirement;
120 FPS at 120 Hz is the target. Raising headset refresh alone does not produce new
game frames. Race resolution remains 1680x1760 per eye, the readable menu stays
1920x1440, and the already accepted fixed foveation remains enabled.

`NFSU2_VR_REFRESH_HZ` accepts `72`, `80`, `90`, or `120`, subject to OpenXR's
supported-rate list. It also enables elapsed-time race pacing. An explicit
value overrides the older `NFSU2_VR_72FPS=1` alias. The jitter regression covers
72 and 120 Hz; actual sustained 120 FPS still requires hardware measurement.
The first three optimization comparisons kept the headset at 72 Hz. The next
run confirmed an OpenXR refresh change to 120 Hz with normal measured race
simulation time. The user confirmed normal speed and movement at least as
smooth as the preceding 72 Hz run. The 120 Hz device setting remains active;
display Hz alone does not meet the game-FPS requirement. Log:
`reports/quest-120-confirmed-optimizations.log`.

`NFSU2_VR_FRAGMENT_SPECIALIZE=1` specializes two fragment-shader checks per
pipeline: alpha testing is removable only for disabled/ALWAYS alpha tests;
HUD clipping is removable only when the vertex path emits constant-positive
clip distances. Texture clip planes, other alpha comparisons, transparency,
depth/stencil behavior, and the tracked HUD clip remain intact. The default
specialization preserves both checks. Pipeline records store the flags in the
previously zero padding word, preserving their size, and shader-source hashes
separate the new SPIR-V. This permits driver early-depth optimization without
forcing early tests, which could otherwise change depth writes on discarded
pixels. GL output is unchanged. Quest shader tests pass; compiled opaque
SPIR-V has no discard instruction, while the general variant retains it.
The user confirmed correct graphics and smoother racing. Log:
`reports/quest-fragment-specialize.log`. This is not a controlled FPS percentage.

`NFSU2_VR_DYNAMIC_DESCRIPTORS=1` tests ordinary immutable descriptor sets with
three dynamic uniform offsets. Every draw binds all seven resources. Identical
image/buffer/range combinations reuse a set within one frame slot; the offsets
still select each draw's own data. Pool chunks grow as needed and reset only
after the frame-slot fence or a completed mid-frame flush. Geometry/uniform
sharing remains enabled, while the known-broken state cache remains off.
The user confirmed correct graphics and smoother racing. Core Vulkan
validation exercised startup draws without descriptor errors; it still reports
the known OpenXR instance-extension dependency issue, and this was not a full
race synchronization validation. Validation was disabled before the driving
comparison. Logs: `reports/quest-dynamic-descriptors.log` and
`reports/quest-dynamic-descriptors-validation.log`.

`NFSU2_VR_COMPACT_BINDINGS=1` groups vertex attributes that already use the same
buffer, stride, input rate and divisor. Attribute byte displacements must fit
`maxVertexInputAttributeOffset`. All 16 attributes, their formats and fetch
addresses remain intact; only the binding descriptions/buffer bindings shrink.
Immutable cross-eye geometry records retain the resulting binding count.
10,000 mixed-layout tests on Quest preserved fetch addresses and input-rate
semantics. In the current race capture, bindings typically shrink from 16 to 2
per draw. The user confirmed correct graphics and smoother racing. Log:
`reports/quest-compact-bindings.log`.

The three non-identical-route captures respectively recorded tracked-window
FPS ranges of 19.8–71.3 (fragment specialization), 22.0–72.0 (dynamic descriptors),
and 23.8–72.1 (compact vertex bindings). These are observations rather than a
controlled speedup comparison or a stable minimum. The dynamic-descriptor run
also hit one retail 250 ms flip-retirement timeout; that synchronization issue
remains unresolved. Dense scenes still need substantial additional work.

After these three accepted changes, a fresh 25-second CPU sample at 120 Hz
attributed about 63% of sampled app CPU time to the pushbuffer/render thread,
including about 41% through `vk_draw_raw`; the main game thread accounted for
about 22%. Copies still account for about 9%, while dynamic descriptor binding
accounts for about 5.4% including its callees. These are CPU sample shares,
not frame-time percentages. The remaining large targets are repeated per-eye
draw preparation/submission and snapshot copying, alongside dense-scene GPU
work. Profiles: `reports/performance-120-compact.data` and
`reports/performance-120-compact-symbols.txt`. No stable 72 FPS minimum has been
demonstrated; the 120 Hz drive still had substantial frame-rate drops.

`NFSU2_VR_SHARED_INSTANCES=1` is an experimental path. The user accepted the
image but clarified that the apparent performance improvement was uncertain.
The first controlled crossover found a regression and it was disabled; the
revised matcher and CPU comparison trials also failed to improve their
stationary crossovers. All remain disabled on the Quest. It also
requires `RECOMP_VK_INSTANCE=1`, `NFSU2_VR_CHECKED_INSTANCES=1`, geometry/uniform
reuse mode 1 and state cache 0. Exact matching still gates each consecutive
instance, separately for each eye. Left-eye group members retain the first
immutable geometry upload and their own contiguous constant offset. Right-eye
groups reuse those ranges without writing them; expired command generations
fall back to individual CPU snapshots. Single draws use a constant-zero vertex
shader specialization. Only merged draws use the instance-index variant.
Pipeline binding is deferred until the final instance count is known, and the
binding cache records the pipeline actually emitted. There is no blind skipping of queued
draws. This preserves cross-eye sharing, which the earlier instancing trial lost.

The Quest constant-range test checked 816 instance addresses and boundary cases
with zero failures. Shader tests and both specialized SPIR-V variants validate;
the single variant removes the instance-index input. Startup Vulkan validation
found only the existing instance-extension dependency error; this does not
validate the full race path. Validation was disabled for the user comparison.
Race logs confirmed shared right-eye groups were active and exact geometry checks
passed; the controlled measurements below subsequently rejected the optimization.
The first capture's 120-frame race windows ranged from 20.1 to 43.1 FPS;
this was not the same route as the earlier baseline. The 25-second CPU sample
still attributes about 60% of sampled app CPU time to the render thread.
Private logs: `reports/quest-shared-instances.log`,
`reports/quest-shared-instances-validation.log`. Rollback configuration is
`build/quest-120-env.txt`, APK `build/quest-confirmed-compact-bindings.apk`.

The first eight-phase stationary crossover measured 16.05 FPS without grouping
and 14.94 FPS with grouping. Average measured per-eye draw CPU time increased
from 31.25 to 36.01 ms at nearly equal raw workloads (9899 vs 9849 draws/frame).
The path was therefore disabled, not counted as a successful optimization.
Evidence: `reports/quest-shared-comparison.log`. A follow-up replaces the old
128-block register loop with seven exact byte-range comparisons against the
held draw's immutable snapshot. It ignores precisely the same non-render-state
methods, uses no dirty-bit assumptions, and skips state comparisons when no
draw is pending. 12,048 register mutation cases match independently
canonicalized register files on Quest. The follow-up
also regressed in its own stationary comparison: grouping off 23.04 FPS vs
on 22.29 FPS (`reports/quest-shared-state-comparison.log`). Grouping was again
disabled automatically. These two scene workloads differed from each other,
so compare on/off within each capture, not between captures.

The next trial adds `NFSU2_VR_INSTANCE_CPU_CHECK=3`: both eyes consume the
same immutable CPU geometry snapshot, and instance equality reads those cached
CPU copies instead of repeatedly reading the GPU upload ring. Contained
interleaved attribute ranges are compared once with exact byte equality.
Mode 3 additionally compares the result to the original GPU-ring checker on
100,000 tracked race candidates before changing to CPU-only mode 1; any
disagreement disables the CPU checker. 10,000 mixed/overlapping-layout tests
match the full independent byte comparison. Pipeline binding is now performed
once when the final instance count is known, avoiding temporary variant binds.
The user confirmed correct graphics, and 100,514 tracked race candidates
matched the GPU checker with zero disagreements. Startup core validation
reported only the known instance-extension dependency issue; full-race
synchronization validation was not performed. The third crossover measured
23.37 FPS off vs 23.04 FPS on, with draw CPU time 21.75 vs 22.17 ms and similar
raw workload (6387 vs 6367 draws/frame). This is no demonstrated speedup.
The comparator disabled grouping automatically; afterwards the previously
accepted `build/quest-confirmed-compact-bindings.apk` and
`build/quest-120-env.txt` were restored on the device. Vulkan validation is off.
Evidence: `reports/quest-shared-cpu-comparison.log` and
`reports/quest-shared-cpu-validation.log`.
Configuration: `build/quest-shared-cpu-comparison-env.txt`.

`NFSU2_VR_SHARED_COMPARE=1` adds an opt-in, eight-phase crossover benchmark
with shared instances off/on/on/off/on/off/off/on. Start it after settling the
car and view by creating the app-private `files/shared-compare.start` file.
Each phase discards 12 transition frames and measures 120 tracked race frames.
It logs average FPS, p50/p95/max frame time, per-eye draw CPU time and raw draw
count. Tracking loss or leaving the race discards the partial phase. Switching
occurs after both eye queues are consumed. After the last phase, grouping stays
enabled only if its aggregate measured FPS beats the ungrouped mode by more
than about 2%; this is a diagnostic selection rule, not a confidence interval.
No extra GPU waits or graphics validation are enabled. This
compares batching within the new renderer; repeated equivalent scene workload
is still required to attribute a speedup, especially for CPU/GPU power drift.
Private comparison configuration: `build/quest-shared-comparison-env.txt`.

Current conclusion: the three tested grouping variants do not advance the
72 FPS minimum. Keep their switches off. The latest stable runtime still uses
1680x1760 per eye, the enlarged 1920x1440 menu, all earlier accepted optimizations
and a 120 Hz display request; 72 real FPS in dense scenes is not achieved.

### Vulkan multiview trial

`NFSU2_VR_MULTIVIEW=1` now enables a separate, opt-in renderer architecture.
It requires stereo, device multiview support for two views, `RECOMP_VK_INSTANCE=0`
and `RECOMP_VK_STATE_CACHE=0`; otherwise the existing path remains active.
Each non-probe target and its depth buffer has two layers. One CPU draw records
both eyes with `viewMask=3`, eliminating right-eye CPU replay and its geometry
snapshot arena. Eye matrices and sampled render-target layers are selected with
`gl_ViewIndex`. Static textures use layer zero; cube probes and the rear-view
camera stay shared. Feedback copies include both layers, and OpenXR presentation
copies the matching source layer into each eye swapchain. View mask is part of
the pipeline key; shader source hashes separate the new sampler ABI. Dynamic
state, pipeline and shading rate are rebound at rendering boundaries.

The standalone `quest_multiview_test`, using `tests/quest_multiview.vert` and
`.frag` compiled to SPIR-V, passed on Quest: all 768 readback pixels matched.
It checks two different eye textures, a shared one-layer texture, independent
depth rejection, a two-layer feedback copy, separate eye presentation blits,
and a mono rendering pass with the same shader ABI. Generated game vertex and
fragment shaders also compile and pass SPIR-V validation. The APK builds and
startup logs confirm hardware multiview is enabled (maximum six views).
The user subsequently confirmed correct graphics in the menu and race. The
captured race logcat buffer contained no additional core-validation errors;
the earlier startup retained the known instance-extension dependency error.
This was not a full synchronization-validation run. Validation was then disabled
and the app restarted for timing. The user again confirmed correct graphics
after driving through dense areas and crashes with validation disabled.
Trial APK/configuration are
`build/quest-multiview.apk` and `build/quest-multiview-env.txt`.
The accepted pre-trial APK/configuration remain available for rollback.

The captured drive contains 64 tracked 120-frame windows: 26.9–74.9 game FPS,
approximately 45.08 FPS aggregated across those windows, with only two windows
averaging at least 72 FPS. Window p95 frame times range from 16.25 to 41.41 ms;
one frame still took 274.34 ms. Dense/world-heavy windows show 4.22–17.27 ms
draw CPU time and 12.91–32.22 ms GPU elapsed time. The right-eye snapshot cost
and arena are both zero. A 25-second CPU sample attributes about 49.2% of app
CPU samples to the render thread, 33.8% to the main game thread, and 4.3% to
render-thread memcpy. This is a different drive than the earlier baseline,
and includes that sampling interval; it does not establish a controlled FPS
speedup. Stable 72 FPS is still unmet. The next large target is GPU work and
render-target traffic, alongside the remaining long-frame stall.
Evidence: `reports/quest-multiview-performance.log`,
`reports/quest-multiview-summary.json`, `reports/performance-multiview.data`,
and `reports/performance-multiview-symbols.txt`. The device remains on the
image-accepted multiview trial, 1680x1760 per eye and 120 Hz, validation off.

### Image-stage barrier trial

`NFSU2_VR_IMAGE_BARRIERS=1` narrows the renderer's global memory barriers to
transfer, fragment shader, early/late depth-stencil tests, color output, and
fragment-shading-rate attachment reads when enabled. Image read/write access
masks and every existing barrier remain intact. The renderer has no vertex
texture fetch, compute work, or GPU writes to vertex/index/uniform buffers;
those buffers are host-written and protected by the existing frame fences.
This allows later vertex processing to overlap earlier fragment processing.
The default remains the original `ALL_COMMANDS` barriers. The standalone
multiview readback test also passes with the narrowed stage mask (768 pixels).

The user confirmed correct race graphics with the new mask. Core validation
was loaded, and synchronization validation was additionally requested through
the Android layer property; the captured race buffer contained no validation
errors. Startup still reports the pre-existing OpenXR instance extension
dependency error. Validation and the extra property were cleared before timing.
`NFSU2_VR_BARRIER_COMPARE=1` reuses the eight-phase stationary comparator with
image barriers off/on/on/off/on/off/off/on. It requires multiview and disabled
instancing; start it with app-private `files/barrier-compare.start`. Resolution,
shaders, uploads and image contents do not change between phases. Each phase
discards 12 frames and measures 120 tracked race frames. The eight-phase result
was 24.18 FPS off and 24.20 FPS on, approximately 5,300 raw draws/frame. This is
not a measurable gain; the comparator disabled it, and the persisted config
also leaves it off. Evidence: `reports/quest-image-barriers-comparison.log`.
Trial APK/configuration: `build/quest-image-barriers.apk` and
`build/quest-image-barriers-env.txt`. Rollback is `build/quest-multiview.apk`
with `build/quest-multiview-env.txt`.

### Feedback target exchange trial

`NFSU2_VR_FEEDBACK_SWAP=1` exchanges the color target and a spare for a proven
full-RGBA screen-space replacement. The original image remains the sampled
input; only the four one-pixel border strips are copied to the new target.
The border preserves raster edge ties. The interior is checked against all
three transformed triangle edges, including a conservative raster margin;
a full-size bounding box is insufficient. A bounded evaluator accepts only
MOV/MUL/ADD and MOV/RCP vertex instructions, rejects indirect constants and
constant writes, and requires finite position and unit W. Blending, alpha or
texture clip rejection, depth/stencil, culling, partial masks/scissors, AA,
unrecognized geometry/programs, and non-multiview paths retain the old copy.
No shader, resolution, texture filtering, frame fence or memory barrier changes.

`NFSU2_VR_FEEDBACK_VERIFY=1` replays the first eight qualifying draws against
the same original input through the copy path and compares every RGBA byte
of both eyes. It retains the reference result and disables exchanges on any
mismatch or readback failure. These GPU waits are diagnostic only and must
be off for timing. On Quest, all eight comparisons passed: 23,654,400 bytes
per draw, 189,235,200 bytes total. The user reported correct menu/race output.
Core validation reported only the previously known OpenXR instance extension
dependency error, with no new errors captured. One full 22.6 MiB copy per
race frame remains (the alpha-only pass); one is replaced by ~54 KiB of border
copies. The standalone 768-pixel Vulkan test passes for copy and exchange,
including untouched edge pixels, two-eye depth/textures and a mono pass.
The evaluator and triangle coverage regression also passes on the headset.

APK: `build/quest-feedback.apk`. Validation evidence:
`reports/quest-feedback-validation-complete.log`. Timing configuration:
`build/quest-feedback-performance-env.txt` (validation/readback off).
`NFSU2_VR_FEEDBACK_COMPARE=1` selects the same eight-phase crossover, started
with `files/feedback-compare.start`.
The completed stationary comparison measured **24.78 FPS off / 24.91 FPS on**
(about 0.5%, below the preselected 2% threshold), with approximately 5,190 raw
draws/frame in both conditions and thermal status 0. It was automatically
disabled, and `build/quest-feedback-disabled-env.txt` now persists that choice
with verification/comparison also off. The running session was not restarted
after the comparison. This trial proves identical output and less copy traffic,
but does not establish a useful FPS gain. Evidence:
`reports/quest-feedback-comparison.log` and `reports/quest-feedback-summary.json`.
Stable 72 FPS remains unmet; 120 Hz is the display request, not the measured
game frame rate. The 250 ms Xbox flip-retirement stall seen in the prior drive
has been traced to `NV097_FLIP_STALL`, but its underlying missed synchronization
has not yet been fixed; the timeout and retail queue semantics remain intact.
Rollback: `build/quest-multiview.apk` and `build/quest-multiview-env.txt`.

### Pushbuffer upload trial

The multiview CPU sample attributes 8.34% of app samples to the scanner itself
and 4.00% to method execution itself, excluding child draw calls. The scanner
still maintained a per-method inventory even without `RECOMP_PB_SCAN`.
`RECOMP_PB_SURVEY=0` skips that diagnostic bookkeeping; an explicit scan request
still forces the inventory on. Decoder control-flow/health history remains.

`RECOMP_PB_BULK=1` coalesces aligned, in-range program/constant vec4 uploads and
non-incrementing inline vertex data. The first word takes the original method
path, retaining initialization. The remainder updates the same register shadow,
dirty bits, upload bytes, cursors and version counters. All other packets,
including drawing, traps, semaphores, unaligned/boundary uploads and truncated
buffers, retain the scalar loop. DMA_GET publication and packet order do not
change. No GPU command, geometry, texture, shader or synchronization is removed.

`RECOMP_PB_BULK_VERIFY=1` checks the first 256 packets of each of the three kinds
against real scalar execution: it saves the bulk result, restores the incoming
state, executes the remaining words normally and compares bytes and state.
It retains the scalar result; a mismatch permanently disables bulk handling.
All **768 packets** passed on Quest (inline vertices, programs and constants),
including registers, dirty flags, cursors and versions. The actual scanner's
standalone test also passed for scalar/packet paths: exact method order, values,
DMA_GET observations, increasing/non-increasing packets, CALL/RETURN and both
JUMP encodings. Explicit diagnostic override remained effective.
Evidence: `reports/quest-pushbuffer-verification.log` and `quest_pb_scan_test`.

APK/config: `build/quest-pushbuffer.apk`, `build/quest-pushbuffer-env.txt`.
`NFSU2_VR_PB_COMPARE=1` selects the eight-phase stationary comparison, triggered
by `files/pushbuffer-compare.start`. It switches both inventory and bulk upload
handling, and disables scalar verification before timing. Other accepted flags
are unchanged; rejected feedback/barrier optimizations and Vulkan validation
remain off. The user confirmed correct menu and race output. The eight-phase
stationary comparison measured **34.80 FPS off / 39.43 FPS on (+13.3%)**, with
approximately 3,270–3,310 raw draws/frame in both conditions and thermal status
0. All four enabled phases outperformed all four disabled phases. The comparator
kept the optimization enabled. `build/quest-pushbuffer-accepted-env.txt` persists
that selection with verification and comparison disabled; no restart was needed.
Evidence: `reports/quest-pushbuffer-comparison.log` and
`reports/quest-pushbuffer-summary.json`. This is a matched stationary improvement,
not proof of stable 72 FPS in dense driving or crashes. The display still requests
120 Hz; race resolution remains 1680×1760 per eye.

The following 45-second driving capture produced 16 complete 120-frame windows:
31.8–48.5 FPS, 41.27 FPS weighted by elapsed time, and a worst captured frame of
43.92 ms. None of those windows reached 72 FPS. No flip timeout was captured in
that interval; this does not prove the earlier rare stall fixed. Evidence:
`reports/quest-pushbuffer-drive-window.log` and
`reports/quest-pushbuffer-drive-summary.json`. The subsequent CPU profile is
`reports/performance-pushbuffer.data` / `performance-pushbuffer-symbols.txt`.

### Isolated vertex input format trial

`NFSU2_VR_VERTEX_LAYOUT=1` skips only a redundant `vkCmdSetVertexInputEXT` when
all binding/attribute descriptions match within the same command buffer,
render-pass generation and bound pipeline. Changes to any of these force a set.
Turning the option off or visiting the replay path invalidates its record.
Unknown extension chains also force a set. Vertex/index buffer offsets, uploads,
descriptors, draw calls, shaders and resolution are unchanged. The previous
`RECOMP_VK_STATE_CACHE` stays off, and this trial requires multiview without
shared instancing. It does not rely on that old cache's descriptor or layout
records.
The conservative reset boundaries follow the Vulkan rules for
[command-buffer state](https://docs.vulkan.org/spec/latest/chapters/cmdbuffers.html)
and [dynamic pipeline state](https://docs.vulkan.org/spec/latest/chapters/pipelines.html#pipelines-dynamic-state).

The standalone `quest_vertex_layout_gpu_test` renders 768 reference pixels using
float and normalized-byte attributes, changing strides/attribute offsets,
one/two bindings, constant attributes, different buffer offsets with identical
formats, stereo depth/texture/feedback and a mono pass. On Quest, always setting
formats (8 sets) and reuse (4 sets + 4 reuses) both produced all expected pixels.
Compile the existing multiview test shaders with `-DVERTEX_LAYOUT_INPUT=1` for
this executable; pass `baseline` or `reuse` as its final argument. Evidence:
`reports/quest-vertex-layout-gpu-test.txt`.

APK/config: `build/quest-vertex-layout.apk`, `build/quest-vertex-layout-env.txt`.
`NFSU2_VR_VERTEX_LAYOUT_COMPARE=1` selects the same eight-phase comparison,
triggered by `files/vertex-layout-compare.start`. The user confirmed correct
menu/race output. Live core validation captured only the previously known
OpenXR instance-extension dependency error, with no new errors. Racing samples
reused about 85% of format commands. Evidence:
`reports/quest-vertex-layout-validation-complete.log` and its logcat companion.
Validation was disabled and the game restarted before timing. The completed
eight-phase crossover measured **38.50 FPS off / 38.72 FPS on (+0.57%)**, around
3,310–3,345 raw draws/frame. This misses the preselected 2% threshold, so the
comparator disabled it; `build/quest-vertex-layout-disabled-env.txt` persists
that selection with comparison off. No additional restart was required.
Evidence: `reports/quest-vertex-layout-comparison.log` and
`reports/quest-vertex-layout-summary.json`. CPU draw work decreased slightly,
but this trial does not establish a useful FPS gain.
Rollback is `build/quest-pushbuffer.apk` plus
`build/quest-pushbuffer-accepted-env.txt`.

### Depth attachment layout trial

`NFSU2_VR_DEPTH_LAYOUT=1` keeps depth/stencil images in
`VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL` after their initial transfer
clear. These private images are subsequently used only as attachments. Their
format, precision, contents, load/store operations and sharing across passes
are unchanged. Each image tracks its recorded layout. A mode change transitions
both depth/stencil aspects and every eye layer after ending rendering, with
the existing conservative ordering retained. The option is off by default.
General layouts are valid but may be less efficient on some implementations;
this trial needs device measurements, per the Vulkan
[image-layout guidance](https://docs.vulkan.org/spec/latest/chapters/resources.html).

The `quest_depth_layout_test` uses D24S8 and a stencil comparison in addition
to the existing two-eye depth/texture/feedback/mono test. It changes layouts
between two draws while loading the existing depth and stencil values. Both
GENERAL→OPTIMAL→GENERAL and the inverse direction produced all 768 expected
pixels on Quest. Evidence: `reports/quest-depth-layout-gpu-test.txt`.

APK: `build/quest-depth-layout.apk`. Configurations:
`build/quest-depth-layout-validation-env.txt` (per-pass GPU timestamps and core
validation enabled for diagnosis), `build/quest-depth-layout-env.txt`
(per-pass diagnostics off for timing). `NFSU2_VR_DEPTH_LAYOUT_COMPARE=1` enables
the eight-phase comparison started by `files/depth-layout-compare.start`.
The vertex-format, feedback-exchange and image-barrier trials remain disabled;
the confirmed pushbuffer optimization remains enabled. The user confirmed
correct menu and race output. Core validation captured only the known OpenXR
instance-extension dependency VUID, with no new errors. Evidence:
`reports/quest-depth-layout-validation-complete.log` and its logcat companion.
Both core validation and per-pass GPU timing were disabled before restarting
for the eight-phase FPS comparison. It measured **39.80 FPS general / 39.57 FPS
optimal**, around 3,100–3,195 raw draws/frame. The trial is disabled because it
does not improve performance. `build/quest-depth-layout-disabled-env.txt`
persists that choice with comparison disabled. Evidence:
`reports/quest-depth-layout-comparison.log` and `reports/quest-depth-layout-summary.json`.

### Bounded GPU pass capture

`NFSU2_VR_GPU_PROFILE=2` allocates the diagnostic query pool but leaves per-pass
timestamps off until app-private `files/gpu-passes.start` is created. The trigger
is consumed in a tracked race; recording stops automatically after 60 tracked
frames. Tracking loss pauses recording. Completed frame slots are read only
after their existing fences, with no extra query waits. `files/gpu-passes.csv`
contains frame/command/pass IDs, target/depth addresses, actual image dimensions,
layer count, draw/vertex/index counts, blend/alpha draw counts, first texture
shader modes and GPU elapsed time for each timed pass. Query failures or the
256-pass-per-chunk limit mark the capture incomplete. The file is closed only
after all captured slots have retired.

These timestamps can affect GPU scheduling and are diagnostic, not an FPS
comparison. Once recording finishes, the heavier timestamps and CSV writes stop
without restarting the race. Mode 0 retains ordinary whole-frame timing; mode 1
retains the previous continuous per-pass summary. Installed APK/configuration:
`build/quest-gpu-capture.apk` / `build/quest-gpu-capture-env.txt`. All rejected
optimization trials and Vulkan validation remain off. The first device capture
completed 60 tracked chunks with zero query/overflow errors and confirmed the
automatic stop. `tools/analyze-quest-gpu.py` ranks the private CSV. In this dense
scene, the timed passes totalled 23.35 ms/frame; two large main-scene passes
accounted for 14.62 ms (roughly 948 and 1,536 draws). GPU busy samples were
96–98% at 640 MHz. These diagnostic timings do not replace an ordinary FPS
comparison. Evidence: `reports/quest-gpu-pass-capture.log`,
`reports/quest-gpu-passes.csv`, `reports/quest-gpu-pass-summary.json` and
`reports/quest-gpu-pass-capture-clocks.json`.

The initially supplied truncated archive and European executable are unsuitable
for these US addresses. The validated US XBE has title ID `4541005A`, entry
`0021B1CE`, and SHA-256
`c8cab1bfe7cf26553e84eb4ef2a26d16f6b06260f7494c583802bc00fd81dd31`.
No discs, generated retail C, private captures, caches, saves or APKs belong in Git.
Restored upstream generator modules are documented in `UPSTREAM-REPAIRS.txt`.
