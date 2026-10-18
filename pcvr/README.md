# Windows PCVR development port

The shared Quest Vulkan/OpenXR renderer now also builds for Windows x64. The
game runs on the PC; Virtual Desktop streams the eye images to Quest 3 through
VDXR. Quest and PCVR share the cockpit, input and VR settings implementation.

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

Seat adjustments use a shared XR eye transform with fixed vehicle/mirror
coordinates. The shared renderer also hides the crossed retail driver proxy
identified in the Pontiac cockpit capture, without removing the car shell or
other drivers. These follow-up fixes are compiled for PCVR; headset validation
is in progress on the native Quest build.
The Lancer follow-up removes the driver-position restriction from that complete
mesh classifier. Stock geometry from all 29 vehicles (2,068 submeshes) passes
on Windows and Quest: exactly 29 crossed driver proxies removed, all remaining
submeshes retained. The separate Lancer/Pontiac capture and mirror fixtures
also pass. PCVR visual confirmation is still outstanding.
The center-mirror search also includes lower glass on 240SX, Eclipse, Lancer,
Miata, RX7 and TT. All 29 center mirrors pass the private stock-geometry tests
on Windows and Quest, with the 23 previously working selections preserved.
The user confirmed the Lancer driver obstruction is gone on Quest and all
three mirrors work there, including after changing the seat position.

The gauge-position follow-up distinguishes the verified `Chyron_IG` driving
notification from real menus. It preserves the seated instruments while the
notification is active; pause, options and dialogs retain the complete menu.
Windows and Quest regression tests cover the captured failing state and
notification/menu combinations. Shared mono cockpit UI no longer replaces the
eye-image foveation map when its resolution differs from the world target.

The shared car-render hook also handles the retail inside-body exclusion
found in the native Lancer Evo VIII capture. Only the current cockpit
player's own body at that specific main-camera call is retained; the original
point/box result remains unchanged for other cars, callers and cameras.
The private stock-geometry audit covers 29 vehicle types and 569 decoded
solids, with 812 lifted-routine/wrapper comparisons passing on Windows and
Quest. This verifies the body-hide fix across vehicle bounds, not the complete
visual appearance of every car or tuning combination. See the audit workflow
in `android/README.md`. This PCVR build still needs a separate headset check.

Validation: Windows and Quest tests pass for settings, bounds, save-format
roundtrip, menu input consumption, variable wheel range/sensitivity, stable
hand contact, and horizontal action pulses. Synthetic mirror variants and the
42-batch Peugeot capture at four seat positions pass. The SDL virtual gamepad
test passes button, trigger, axis, deadzone and detach checks on Windows.
Other-car visuals, physical gamepads, menu placement, music-change stutters and
PCVR reconnection still require headset testing.

## Build and launch

### Shared cockpit prototype

Use the normal camera-change control while driving to cycle into Cockpit
View, after the four usual driving camera slots. No separate button or long press is
needed. Both stick clicks recenter. All cars currently share one interior with both door cards,
rear bench and floor from one Tripo model in anthracite/blue, and a
steering wheel animated from player-one steering input.
The build copies `cockpits/shared.ncpk` beside the EXE. Keep this folder when
moving the application. See [asset details](../assets/cockpits/README.md).
The Windows build and math/asset tests pass. The full cabin, normal camera cycle
and wheel animation are user-confirmed on native Quest; PCVR cockpit rendering
still needs a separate headset test.

Touch controls now use right stick click to cycle cameras and left stick click
to hold rear view outside Cockpit View. The cockpit has no button-driven rear
view; turn your head there. Grip near the rim enables one- or two-hand motion
steering with translucent ghost hands, visible only in the cockpit. Releasing
both grips retains the last steering angle until grabbing again or deliberately
deflecting the left stick at least 25%. Stick steering then remains active,
including centering on release, until the next grab. Either grasped hand takes
priority over the stick; small stick drift does not release a held angle.
Tracked wheel input compensates for the game's 24% stick deadzone, allowing
small wheel turns to steer proportionally. Stick/gamepad deadzones remain intact.
A new grab starts at the stick's effective steering to avoid a response jump;
the displayed wheel and held gloves still follow the physical wheel angle.
Diagonal hub crossings and pose interruptions retain the grip and rebase
angular tracking without a steering jump. Real Grip release still takes effect
without a pose; unavailable button input is tolerated for up to 500 ms.
Held gloves keep the same wheel-local acquisition point through these events;
only release/regrip changes the contact location. The hand-only jumping fix
passes Windows/Quest pose tests and was accepted on native Quest.
Default wheel travel is 360 degrees lock to lock, with half a turn left and half
a turn right; rotation and sensitivity are adjustable in VR Settings. Both clicks recenter. The shared
grip/control tests pass on Windows and Quest; PCVR hand interaction is untested.
Stick-click actions use the game's analog White/Black buttons. Grips no longer
send those buttons, so gripping cannot change cameras, and the right click
remains usable to leave the cockpit while steering.
While driving, Touch A is nitro, B is handbrake, and right stick up/down shifts
gears once per deflection (return to center to shift again). Use the game's
manual transmission setting. In menus A/B and right-stick navigation retain
their original behavior. The shared headliner is removed and the eye is lower
and closer to the wheel; this fit is user-confirmed on Quest. The mirror glass
detector now uses car-relative bounds and the same seat definition as the camera.
The new driving mapping and mirror repair pass packet/geometry tests and are
user-confirmed on native Quest. PCVR headset validation remains pending.

The shared renderer also maps the live 512x128 rear-camera image onto the
original interior and outside mirror glass, using three sectors of one rear
camera, not three separate reflection cameras. The additional floating mirror
rectangle is removed. The cockpit map is lower left and the smaller speed
instrument is behind the wheel, composed before the wheel and hands without an
added black backing. See [implementation and verification](../android/README.md).
Pause/options menus use a complete panel selected from the active frontend
package list, retaining both lower corners; the HUD split returns during driving.
The Windows build and geometry tests pass; PCVR visual behavior needs a separate
headset test. The user accepted the native final instrument/mirror layout.

The shared OpenXR code also forwards the game's vibration strengths to both
Touch controllers through VDXR, using the same bounded pulses and focus handling
as the Quest build. Enable **Vibration** in the game; `RECOMP_RUMBLE=0` disables
XR output. See [Touch vibration details](../android/README.md#touch-vibration).
The Windows haptic timing tests pass; physical PCVR feedback remains untested.

### Headset connection at startup

The host waits up to 15 seconds for a temporarily unavailable headset before
showing Connect/Retry. Session creation also retries transient runtime failures;
a failed `xrBeginSession` is retried while the session remains READY. Fatal
renderer initialization and lost-session errors offer an app-only restart
instead of leaving game audio running behind a black console. Connect the
headset to the chosen OpenXR runtime before selecting Retry. The reported
first-boot failure has not yet been reproduced with this build; external
streamer/decoder stalls are outside this recovery path.

### First launch: import your Xbox ISO

The Windows application checks for all 12 required game files before starting
SDL or OpenXR. If the files are missing or incomplete, it automatically opens
an English **Game data setup** window. Choose **Select ISO...** and select your
unmodified **Xbox USA / NTSC-U ISO or XISO**. Bare game-partition images and
the supported full-disc/video-partition layouts are accepted. PC and European
discs are not supported. Compressed archives must be unpacked to an ISO first.

Import runs in the background with progress and cancellation. Every imported
file is checked against the same sizes and SHA-256 hashes as the Quest importer.
Allow approximately 2.5 GB of free space in addition to the source ISO. The ISO
is read in place and remains untouched. Existing saved games and unrelated
files are preserved. A failed or interrupted installation cannot boot; setup
can be retried. Only one instance can use a game-data folder at a time.

Once verification succeeds, PCVR starts automatically. Later launches skip the
dialog when the required file sizes and executable checksum are correct.
The development launcher uses the repository's `game/` folder, including an
explicit `-GameDirectory` path that does not yet exist. A direct EXE launch
uses an existing complete `game/` in the working directory or beside the EXE;
otherwise it installs to `%LOCALAPPDATA%\NFSU2VR\game`. `NFSU2_GAME_DIR` overrides
that choice. The release launcher keeps data in its own `game/` folder.

No Python, Java, external extractor or mounted virtual DVD is needed to import
an ISO in the built PCVR application. Building the game from source still
requires your own disc and locally generated game code as described below.

### Build from source

Requires Visual Studio 2022 C++ x64 tools, CMake, Ninja, a working Windows Vulkan
driver and Vulkan headers. The existing source dependencies in `build/deps`
(`SDL`, `glslang`, `OpenXR-SDK`) are shared with the Quest build; their pinned
versions are recorded in `android/dependencies.json`. Generated US Xbox game
code must already exist in `gen`, with the extracted disc in `game`. Game assets
and generated retail code are private build inputs and are not included here.

From the repository directory in PowerShell:

```powershell
./tools/build-pcvr.ps1 -VulkanHeaders 'C:/VulkanSDK/<version>/Include'
./tools/start-pcvr.ps1
```

`-VulkanHeaders` also accepts an NDK `sysroot/usr/include` directory. Only its
Vulkan headers are copied into the Windows build; Android system headers are
not used. An import library is generated from the installed Windows Vulkan
loader. No driver DLL is redistributed.

Connect Quest 3 to this PC in Virtual Desktop before starting the game. The
launcher selects the installed VDXR manifest for this process without changing
the system OpenXR setting. `-GameDirectory 'D:/Games/NFSU2-Xbox'` selects another
extracted disc directory. Saves stay with the game. Keep `SDL2.dll` and
`openxr_loader.dll` beside `build/pcvr/game/nfsu2-pcvr.exe`.

The launcher loads `pcvr/benchmark.env` and writes timestamped logs and shader
caches under `build/pcvr-run`. `current.json` identifies the active process and
log. `-Wait` keeps the launching shell attached and reports the exit code.
The host opens stdout/stderr log files directly; PowerShell does not relay them
through anonymous pipes. Routine buffered flushes run on the host service.
Touch controls use the shared Quest mapping: A / left menu to advance, right
stick for menu navigation, both stick clicks to recenter.

## Measuring game FPS

The current target is stable **72 game FPS**; the user replaced the earlier
120-FPS target and set Virtual Desktop to 72 FPS on 2026-10-14. The comparison
configuration uses 1680×1760 per eye, a 1920×1440 menu texture
and a requested 72 Hz display rate. Runtime refresh and newly rendered game
FPS are separate measurements. Foveated shading is used only if the PC driver
supports the renderer's required Vulkan capability; check the startup log.

Let the shaders warm up, enter a race and keep the headset active. Start a
45-second recording from another PowerShell window:

```powershell
Set-Content build/pcvr-run/benchmark.start 'start'
```

The host service checks the trigger file outside the graphics thread. The
renderer records game frame intervals in RAM. It writes `benchmark.csv`
afterwards and prints `[PCVR] benchmark complete` with the frame count, duration
and average. Losing race/tracking activity aborts recording. Always require a
fresh completion line: an older CSV is not evidence of a completed new run.

```powershell
./.venv/Scripts/python.exe tools/analyze-pcvr-fps.py build/pcvr-run/benchmark.csv --refresh-hz 72
```

The report includes average FPS, frame-time percentiles, worst frame, 1% low
(reciprocal of the mean slowest 1% of intervals), and the fraction of frames
inside the 72/120 FPS time budgets. It excludes transport/decoding latency and
synthetic frames added by the runtime. Compare the same race, view and settings;
also record Virtual Desktop streaming settings when comparing end-to-end feel.

The saved standalone Quest driving reference is **41.27 FPS**, with 120-frame
windows from **31.8 to 48.5 FPS** and a worst recorded frame of **43.92 ms**:
`reports/quest-pushbuffer-drive-summary.json`. That run is a historical reference,
not a synchronized route replay. Stable 72 game FPS has not yet been
demonstrated by the PCVR build. Historical 120-Hz results below retain their
original settings and must not be treated as matched 72-Hz comparisons.

## Verified so far (2026-10-14)

- MSVC Release build succeeds; VDXR initializes on RTX 3050 Laptop GPU and
  reports a focused session at 120 Hz with both eye swapchains.
- Standalone Vulkan stereo/depth/texture/feedback/presentation checks pass:
  768 checked pixels, zero failures. See `reports/pcvr-multiview-test.txt`.
- The user reached a stereo race with working Touch controls, then reported
  missing cars, audio crackle, stutters and a frozen picture. The first timed
  benchmark did not finish and must not be presented as a valid comparison.
- The frozen Windows thread stacks show a retail D3D event wait whose fence
  had already completed (`target=0x16a85`, `completed=0x16a93`). The new Windows
  completion predicate reads the real semaphore for that specific wait while
  preserving retail submission. Its initial placement covered missed events
  but not early events; the correction below addresses that omission. Ten completion
  cases, including sequence wraparound, pass. The next headset test lasted
  more than a minute without freezing, and all cars were visible. Brief car
  movement glitches during the starting-line sequence remain unverified.
- Windows hardware-register workers now sleep when idle and wake on a guest
  poll, instead of continuously consuming CPU with `Sleep(0)`.
- Guest `KeTickCount` uses QPC milliseconds on PCVR: the local `GetTickCount`
  probe advanced in 15–16 ms steps. The user still reported audio crackle and
  small stutters after this change; it does not establish an audio fix.
- Windows log output is now buffered and periodically flushed by the host
  service. The user initially reported less stuttering and better audio, then
  confirmed that microstutters and audio clicks remain. This is not a complete
  audio or pacing fix.
- A later freeze (PID 17492, `stderr-20261013-220438.log`) followed two decoder
  `bad target` errors. The guest waited with completed fence `0x3503b` and
  issued fence `0x35077`, despite DMA_GET equalling DMA_PUT. Unlike the earlier
  lost notification, real commands had been skipped by decoder resynchronization.
  Missing car parts and buildings disappearing on head turns also remain open.
  These failures invalidate an acceptance claim based on a short successful race.
- `pcvr_pb_boundary_test` reproduces reading unpublished payload when PUT splits
  a method packet (8/12 failures before the Windows fix). The decoder now keeps
  pending method/count state and bounds bulk uploads at PUT. All 21 cases pass,
  including split non-incrementing packets, CALL/RETURN, both jump encodings and
  an explicit driver reset. This establishes decoder correctness for those
  cases; it does not yet prove the observed race freeze had that cause.
- The subsequent VR run (PID 20368, `stderr-20261013-221851.log`) still read
  overwritten ring data and eventually crashed with an access violation.
  `reports/pcvr-pb-fault-20368.txt` preserves the first fault's header history.
  The split-packet correction therefore was not sufficient.
- The Windows D3D completion predicate is now authoritative even when its
  event is signalled. Previously a delayed NOP notification could release a
  later, still-pending fence, allowing premature reuse. The production
  dispatcher regression test exercises stale events, completion without an
  event, timeout and unrelated auto-reset events: two failures with the old
  dispatch ordering, zero with the correction (five scenarios). The automatic
  desktop run `stderr-20261014-140944.log` intercepted at least eight such
  early events. GPU semaphore writes also wake registered completion waiters
  directly, avoiding the usual 4 ms polling delay. This does not yet prove
  that the VR corruption, freezes or microstutters are resolved on its own.
- Automatic desktop run PID 16884 (`stderr-20261014-141135.log`) reached a
  race (frame 3600 screenshot) and continued beyond frame 8100, including
  driving against roadside barriers. No decoder bad-target errors or crash
  occurred before the test was stopped deliberately. Early events were
  intercepted again. Frame-time spikes up to 166 ms remained in this run;
  desktop screenshots also introduce known stalls on frames 3601/5401/7201.
  This is a stability check at desktop pacing, not a VR FPS comparison.
  The latest build additionally measures Vulkan pipeline creation and cache
  save time, which the older program-compilation counter did not include.
  VDXR launch at 14:08 on October 14 failed with
  `XR_ERROR_FORM_FACTOR_UNAVAILABLE`. The connected-headset retest below followed.
- In the subsequent VDXR run (`stderr-20261014-181117.log`, PID 12984), the
  user completed a race and explicitly confirmed that microstutters and audio
  problems were fixed in that test. No decoder bad-target errors or crash were
  logged; early D3D events were intercepted repeatedly. Missing car parts,
  jumps during the race introduction, side-view building disappearance and
  distant scenery popping in remain open. The short frame-time windows are
  not a completed 45-second acceptance benchmark.
- Disabling only the primary camera planes at runtime improved building
  visibility and made all opponents appear, but did not fix missing lower car
  parts or the side-view boundary. The diagnostic was restored to normal before
  rebuilding. A second independent world-object gate was found in
  `sub_001C6900`: after the tracked box traversal, it rejects bounding spheres
  behind the original game camera before computing distance-based LOD.
  The PCVR correction retains such a sphere only if it intersects the cached
  tracked frustum of that same primary view. Distance-based LOD and auxiliary
  cameras retain their previous behavior. `NFSU2_PCVR_WORLD_VISIBILITY=0`
  disables this correction for comparison. The expanded culling regression
  test covers rear-hemisphere objects revealed by a head turn, sphere/plane
  intersection, scaled planes and invalid inputs. Build and culling, fence,
  dispatcher and pushbuffer tests pass. The headset retest retained the same
  missing-parts and disappearing-building defects; the user confirmed no new
  defects, but this change did not resolve the observed problem. Runtime rescue
  counters prove that the branch executes, not that the visual defect is fixed.
- `NFSU2_PCVR_HEALTH=1` enables approximate I/O/audio counters, hitch phase
  timings (including main-thread guest-lock and GPU interrupt waits) and a
  bounded decoder history saved as `pcvr-pb-fault.txt` on the
  first invalid target. Do not use diagnostics or framebuffer dumps for an FPS
  acceptance run. The temporary `pcvr-culling.start` value `1` disables primary
  CPU frustum rejection only; `0` restores tracked planes. This experiment is
  not a production fix and resets to normal on process restart.
- The subsequent GPU captures (`reports/pcvr-visibility-20261014-183618`
  and `reports/pcvr-visibility-20261014-183658`) contain 1,389 and 1,337
  perspective world draws respectively. All use the full 1680×1760 eye
  scissor; the captured MVP depth rows match the selected projection within
  0.01. These checks exclude two candidate causes in those frames, but do
  not prove that absent geometry reached the renderer. The attempted timed
  benchmark in PID 25772 aborted on inactive tracking; no new FPS acceptance.
- `tools/start-pcvr.ps1 -Capture` enables opt-in one-frame diagnostics without
  the native comparison or Vulkan validation overhead. `frames/xr-trace.request`
  contains a draw stride (for example 100) and records the draw CSV, final eyes
  and intermediate surfaces, including the multiview path. The CSV now includes
  the actual Vulkan scissor, projection and MVP rows. `frames/cull-trace.request`
  requests one primary-camera update interval of CPU box decisions, with caller,
  view, world bounds and the rejecting plane in `frames/cull-tests.csv` (bounded
  to 50,000 records). File polling is on the host service; recording is off by
  default and must not be used for FPS acceptance. Visibility behavior itself
  is unchanged in this diagnostic build.
- `pcvr_threads.exe PID` captures native thread stacks for a still-running
  frozen process. Keep its matching game PDB beside the EXE. The launcher option
  `-Diagnostics` enables native-vs-lifted comparisons, audio tracing and bounded
  frustum captures; do not use that mode for the final FPS comparison.
- The user clarified that disappearing scenery updates while driving; head
  turns at a standstill leave the existing visibility selection unchanged.
  Retail world selection has a speed-dependent PVS gate (`0x3cac88 == 3`,
  threshold 17.8811 units/s); this remains unmodified and is not established as
  the cause. The saved Quest performance options match PCVR's renderer options.
- The captured world shader completes `oPos.x` with DP4 and computes RCC of
  `oPos.w` in the same instruction. The Vulkan raw-clip detector previously
  missed that sequence. It now captures after that pair when the ILU cannot
  overwrite the position. The expanded `pcvr_shader_test` fails twice before
  and passes after the change, including incomplete-position, unrelated
  reciprocal, destructive-output and GL exclusion cases. The headset retest
  still had missing cars/parts, scenery and rain defects: this alone did not
  fix the reported problem.
- A direct host comparison found a missing Quest startup invariant:
  `quest_entry.c` forces `RECOMP_CULL=0` for OpenXR, but the PCVR host had left
  the NV2A executor's old screen-space batch rejection enabled. This test
  operates before the VR renderer using the original flat viewport, so it can
  discard geometry that belongs in the tracked view. PCVR now applies the same
  setting after XR initialization and before starting guest execution; desktop
  behavior and the tracked CPU/Vulkan culling remain unchanged. A startup log
  records the policy (`stderr-20261014-185612.log`, PID 13036). Release build,
  shader and culling tests, completed-fence checks, five dispatcher scenarios
  and 21 pushbuffer boundary cases pass. In this VDXR run the user confirmed
  all three reported defects fixed: complete start-line cars/parts, stable
  buildings during driving/head turns, and rain remaining visible at speed.
  This confirms the missing host policy as the decisive correction in that
  comparison. The PVS gate remains unchanged. The installed standalone Quest
  APK has not been rebuilt. Longer-session and FPS acceptance remain separate.
- The accompanying completed 45.004-second driving capture contains 3,291
  real game frames: **73.13 FPS average**, p95 **21.01 ms**, p99 **27.32 ms**,
  1% low **18.61 FPS**, worst **179.38 ms**. Only **59.10%** of intervals meet
  the 72-FPS budget and **6.75%** meet 120 FPS, so the minimum is not stable.
  Evidence is preserved in `reports/pcvr-visibility-fixed-20261014-185612/`
  (CSV, summary, runtime log, options). This was the user's visual driving
  test, not a matched replay of the historical 41.27-FPS Quest route. Several
  long frames include 77–163 ms waiting for GPU interrupt servicing, one has
  64 ms in XR end, and heavy scenes reach over 5,000 draws; no shader/texture
  compilation accompanied those recorded hitches. These are measurement
  findings, not evidence that the earlier audio/synchronization fixes should
  be reverted.
- This is a development port. Longer play sessions, saves and audio continuity
  have not yet been validated on Windows.

## CPU hitch investigation (2026-10-14)

The opt-in `NFSU2_PCVR_IRQ_PROFILE=1` separates dispatch-lock wait, vblank,
interrupt, DPC and timer work, plus slow guest-lock holds and ISR bodies.
Keep it unset for FPS acceptance. The diagnostic logs
`reports/pcvr-irq-profile-20261014-190126.log` and
`reports/pcvr-irq-detail-20261014-190457.log` located 55–183 ms delays in the
vblank wrapper, outside the actual ISR. It unconditionally flushed stderr on
every tick, even when it had printed nothing. PCVR now flushes only its first
three startup messages there; the host service handles ordinary output.
Other platforms keep their prior behavior. No fence, dispatch ordering or
visibility check was relaxed.

The subsequent driving capture (`reports/pcvr-vblank-flush-fixed-20261014-191017`)
averaged 96.43 FPS, p95 16.60 ms, p99 19.06 ms and worst 133.23 ms. It includes
race startup and is not a matched replay of the 73.13-FPS baseline. The user
confirmed correct graphics but judged the stalls about unchanged. A dense
follow-up averaged 61.56 FPS. Direct-file logging then measured 60.19 FPS in
another dense stop (`reports/pcvr-direct-log-dense-20261014-191640`), p95 19.82 ms,
p99 22.57 ms, worst 43.27 ms. These different scenes do not establish a frame
rate improvement from logging alone; the sustained stationary slowdown remained.

A bounded local CPU sampler (`pcvr_cpu_sample PID SECONDS`, 1–20 seconds)
selects the four busiest game threads and copies their instruction pointers,
resuming each before aggregation or symbol lookup. This is intrusive diagnosis,
not an acceptance benchmark. It accepts only `nfsu2-pcvr.exe`. Build the
`pcvr_cpu_sample` target separately. The sample in
`reports/pcvr-cpu-19588.txt` places many renderer samples immediately after reads
of freshly written `VsBlock`/`FsBlock` fields in mapped upload memory. Coherent
Vulkan upload memory is not necessarily CPU cached. PCVR now constructs both
blocks in ordinary CPU memory, performs the same classifications, then copies
the finished blocks into the same ring allocations. The shader values, resources
and draw order are preserved; standalone Quest code is unchanged.

`NFSU2_PCVR_CPU_UNIFORMS=0` selects the original construction path. With
`NFSU2_PCVR_HEALTH=1`, write `0` or `1` to `pcvr-uniforms.start` in the run
directory to switch the reference/optimized path at a draw boundary for a
stationary comparison. The default is optimized on each launch. This control
does not change Vulkan resource bindings or synchronization. Shader regression
checks and the 768-pixel Vulkan stereo/depth/texture/feedback/presentation check
pass. The user confirmed correct menu/race graphics with CPU construction.
The subsequent optimized dense capture averaged 75.07 FPS (p95 15.41 ms,
p99 17.22 ms, worst 32.57 ms); only 74.19% of intervals meet the 72-FPS budget.
The first reference capture changed from roughly 1,889 to 3,830 draws per frame,
whereas the optimized capture remained around 3,800. Their average FPS must
not be reported as a matched comparison. The diagnostic windows show CPU work
per draw dropping from approximately 2.54 to 1.73 microseconds. A third capture
contains a late mode switch and is excluded from FPS acceptance. All three
recordings and these limitations are retained under
`reports/pcvr-uniform-ab-20261014-191920/`. Stable 72 FPS is still unproven.

The launcher accepts `-EnvironmentOverrides @{NFSU2_OPTION='value'}` for
process-local experiments after loading the comparison preset. Override keys
are restricted to `NFSU2_` and `RECOMP_` options. The isolated vertex-format
reuse GPU check can be built as `pcvr_vertex_layout_test` and run with the
`VERTEX_LAYOUT_INPUT=1` multiview shaders and argument `reuse`. On this NVIDIA
GPU it passes all 768 pixels, including format, buffer and mono/stereo pass
changes (four layout sets, four reuses). The subsequent stationary headset
ABBA/BAAB comparison retained correct graphics and measured 70.68 FPS without
reuse versus 73.87 FPS with reuse (+4.51%, roughly 4,000 draws per frame).
Evidence: `reports/pcvr-vertex-layout-20261014-192626/`. The isolated option is
now enabled in `benchmark.env`; the general Vulkan state cache and instancing
remain disabled. This average does not establish stable 72 FPS.

Texture-upload and shader-compilation timers now run only on cache misses,
removing clock queries from already-cached draws. The following 45.010-second
dense stop measured 67.43 FPS, median 12.55 ms, p95 17.21 ms, p99 99.04 ms,
worst 115.46 ms; 82.41% of intervals meet the 72-FPS budget. The recording is
`reports/pcvr-cache-timing-dense-20261014-193015/`. It contains a burst of long
XR end waits and is not a matched before/after test of the timer change.
A separate thread snapshot (`reports/pcvr-vdxr-stall-stacks-5488.txt`) finds
the renderer waiting inside VDXR's `xrEndFrame`, and VDXR's submission worker
waiting inside its LibOVR runtime. This locates the wait, without establishing
its cause. In lighter driving windows the same process reaches about 120 FPS.

`tools/compare-pcvr-cpu-sets.ps1 -InspectOnly` reports the hybrid CPU topology.
Without that option it performs a 48-second ABBA comparison of the original
process CPU-set assignment and the faster CPU class, then restores the original
assignment in `finally`. It operates only on this workspace's current game
process, changes no system power policy, and saves phase byte offsets into its
runtime log. Exclude the first three seconds of each phase and log windows
crossing a phase boundary. This experiment is separate from a complete frame
time acceptance capture; no CPU scheduling override is enabled by default.

The matched CPU-set experiment (`reports/pcvr-cpu-sets-20261014-193931/`)
retained approximately 4,520 draws per frame. Its two original-assignment
phases averaged 75.09 and 74.93 FPS in complete 120-frame windows; the faster
class phases measured 73.69 and 51.96 FPS, the latter including a burst of
VDXR waits. No benefit was established; the original assignment was restored.

An opt-in `NFSU2_PCVR_PB_DIRECT=1` experiment bypasses the redundant first-word
scalar dispatch for bounded inline, program and constant uploads. It preserves
one-time initialization and checks all components against the real scalar
executor when `RECOMP_PB_BULK_VERIFY=1`. Each kind passed 256 comparisons of
payloads, shadow registers, dirty flags, cursors and versions; 21 pushbuffer
boundary cases, completed-fence checks and five dispatcher scenarios also pass.
The subsequent headset report was severe 11-FPS operation, over 400 ms latency,
and a delayed right eye. The experiment is disabled by default and explicitly
off in the preset. Restarting with the reference path did not remove that
symptom: even menu frames with about 0.2 ms draw CPU time waited about 90 ms in
XR end. Keep this experiment out of acceptance until the presentation problem
is resolved. Diagnostic log: `reports/pcvr-direct-packets-rejected-20261014-194118.log`.

The user then reported VD overlay times of Game 7 ms, Encoding 24 ms,
Networking 0 ms and Decoding above 400 ms. The PC GPU had about 650 MiB of
4 GiB VRAM allocated during the affected menu, excluding VRAM exhaustion in
that state. After restarting the PC Streamer and the Quest Virtual Desktop
client, the user reported Decoding 8 ms. With the packet experiment still off,
a completed 45.004-second driving capture recorded 5,000 game frames:
111.10 FPS average, p95 11.47 ms, p99 12.93 ms, worst 21.02 ms, 1% low
69.74 FPS; 99.56% met the 72-FPS budget. Evidence:
`reports/pcvr-decoder-recovered-20261014-194517/`. This supports a separate
streaming-path stall in the severe 11-FPS state; it does not establish the
trigger or excuse the remaining dense-scene CPU cost. The route is not a
matched dense stationary acceptance test and stable 72 FPS is not yet proven.

PCVR now stores the tracked-MVP classification with each immutable compiled
shader program. The decision depends only on the microcode already in the
shader cache key; per-object constants, head tracking and shader output remain
unchanged. The first 100,000 actual classifications are checked against the
original scan; any mismatch permanently falls back to the scan for that run.
Other platforms retain their original analysis. Shader regression checks and
the 768-pixel Vulkan stereo/depth/texture/feedback/presentation test pass.

The metadata classification passed 100,000 real draws. With that version,
the user again reported Decoding near 200 ms, then recovery after returning to
the game on the PC. Reported VD settings were HEVC 10-bit, 47 Mbps, Medium,
Video buffering enabled; they were not changed. A deliberate subsequent
window-switch test did not reproduce the decoder spike. This is a correlation
to investigate, not a confirmed focus bug or a proven codec problem.

`tools/record-pcvr-focus.ps1 -Seconds 50 -Benchmark` records a fresh 45-second
game-frame capture plus foreground PID/category and this game's Windows power
throttling masks once per second. It changes no focus, scheduling or streaming
settings and records no window titles/content. The CSV is archived only when a
new benchmark completion appears in this run's log. Without `-Benchmark`, it
only records process metadata. The dense stationary capture in
`reports/pcvr-focus-20261014-195502/` averaged 74.81 FPS, p95 15.46 ms,
p99 18.67 ms, worst 39.53 ms; 72.11% met the 72-FPS budget. The process already
reported ControlMask 5 / StateMask 0 (execution and timer throttling disabled).
Both that capture and the separate switch observation
(`reports/pcvr-focus-20261014-195622/`) recorded the same non-game foreground
PID throughout, so they do not establish a host foreground transition.
Stable 72 FPS remains unmet in this dense view.

`NFSU2_PCVR_PB_DIRECT_COMPARE=1` adds a same-scene ABBA/BAAB comparison of the
existing direct data-packet experiment. Launch with `NFSU2_PCVR_PB_DIRECT=1`
and `RECOMP_PB_BULK_VERIFY=1`. All three direct packet kinds must first pass
256 comparisons against the real scalar executor (verification counts are
separate for direct and reference modes). Once settled in a tracked race,
create `direct-packets-compare.start` in the run directory. Verification is
disabled before timing; the executor switches paths only at completed frame
boundaries. Each phase discards 12 frames and measures 120. The regular preset
still leaves direct packets off until the comparison and visual test pass.

The completed direct-packet comparison retained correct graphics but measured
77.28 FPS reference versus 77.70 FPS direct (+0.54%, approximately 4,123–4,217
draws/frame). It did not exceed the 2% decision threshold, so the live run and
default preset retain the reference path. Eight phases are preserved under
`reports/pcvr-direct-packets-20261014-200100/`; this short crossover is not a
full frame-time acceptance capture.

An opt-in `NFSU2_PCVR_SURFACE_LOOKUP=1` memoizes the original color-surface
address/eye lookup, including negative results. All entries are invalidated
before the sole surface creation/replacement path changes a slot, including
creation failure and reference-mode comparison phases. It caches table slots,
not Vulkan image handles or draw state; shared-eye and first-slot semantics
remain identical. The first 100,000 real queries are compared against the
original search, with a permanent reference fallback on mismatch. The isolated
`pcvr_surface_lookup_test` covers creation, deletion, replacement, both eyes,
shared surfaces, negative results, address zero, and deliberate hash collisions
(520,013 queries, zero failures). The 768-pixel Vulkan multiview regression
also passes. The default preset now enables this after the comparison below.
To compare, additionally set `NFSU2_PCVR_SURFACE_LOOKUP_COMPARE=1`, wait for
verification, then create `surface-lookup-compare.start` in the run directory;
the same eight-phase crossover runs without changing resolution or draw order.

The user confirmed correct menu/race, reflections and both eyes. All 100,000
live lookup comparisons passed. The crossover measured 76.82 FPS reference
versus 79.62 FPS cached (+3.64%, roughly 4,316–4,409 draws/frame); each optimized
phase was faster than its neighboring reference phases. Evidence is under
`reports/pcvr-surface-lookup-20261014-200524/`. The following 45.006-second dense
capture (`reports/pcvr-focus-20261014-200606/`) averaged 77.43 FPS, p95 15.06 ms,
p99 16.43 ms, worst 32.18 ms; 81.66% met the 72-FPS budget. The lookup is enabled,
but this longer capture still does not meet stable 72 FPS.

`NFSU2_PCVR_PB_SHADOW=1` is a separate, opt-in data-packet experiment. It updates
contiguous CPU shadow registers by dirty block instead of testing/writing each
word. Non-incrementing packets retain the final value and latch intermediate
changes, including when the final value restores the original. The original
first-word method dispatch, actual data upload, version counters, fences and
command ordering are retained. `pcvr_bulk_shadow_test` compares full register
and dirty arrays against scalar writes (47,260 cases, zero failures), including
block boundaries, 2,047-word packets and unrelated dirty flags; the 21 real
pushbuffer boundary scenarios also pass. With `RECOMP_PB_BULK_VERIFY=1`, all
three packet kinds must pass 256 live full-state comparisons before
`NFSU2_PCVR_PB_SHADOW_COMPARE=1` accepts `shadow-blocks-compare.start`. Timed
phases disable verification. The default preset leaves this experiment off.

The shadow-block test retained correct graphics but did not demonstrate a
speedup. The aggregate was 79.45 FPS reference versus 78.52 FPS optimized;
the first reference phase had fewer objects (4,051 versus approximately
4,279–4,361 later), so this is not a precise regression estimate. The remaining
phases likewise do not show a consistent advantage. Both live mode and preset
are off. Evidence: `reports/pcvr-shadow-blocks-20261014-201026/`.

The CPU sample in `reports/pcvr-cpu-8100.txt`, with executable/base metadata and
separate Vulkan/pushbuffer symbol mapping, locates repeated scalar index
rebasing in `raw_batch`. `NFSU2_PCVR_INDICES=1` tests SSE2 unsigned min/max and
four-index rebasing on Windows x64 only. No index is dropped or reordered,
unaligned inputs and scalar tails are preserved, and the original culling
decision/order is unchanged. `pcvr_indices_test` passes 24,608 cases including
zero lengths, all alignments, the 65,536-index batch limit, unsigned extrema and subtraction wraparound;
pushbuffer boundaries and the 768-pixel Vulkan multiview test also pass. The
first 10,000 real batches compare both range and every resulting index against
the original calculation, falling back on any mismatch. For a stationary
comparison, enable `NFSU2_PCVR_INDICES_COMPARE=1` and create
`index-data-compare.start` after verification. The default preset remains off
pending the live image and performance comparison.

The user confirmed correct graphics for the index range/rebase variant.
Its short crossover (`reports/pcvr-indices-20261014-201745/`) measured 72.85 FPS
reference versus 74.92 FPS optimized, but phase results were inconsistent and
included scheduling outliers. The following 45.013-second capture
(`reports/pcvr-focus-20261014-201817/`) averaged 73.31 FPS, p95 15.61 ms,
p99 21.17 ms and worst 112.92 ms. This does not establish a dependable gain or
stable 72 FPS; the default remains off. Later in the same process, Virtual
Desktop's refresh changed to 72 Hz and another burst of long XR-end waits
appeared. The process had exited before the next build; the exit cause was not
captured.

`NFSU2_PCVR_INDEX_PACKETS=1` is a separate experiment targeting the repeated
ARRAY_ELEMENT16 dispatch seen in `reports/pcvr-cpu-9620-pb.txt`. Only bounded,
non-incrementing, subchannel-zero index packets with room for every index are
batched; all other packets retain scalar execution. The original first method
preserves initialization. SSE2 expands low/high 16-bit indices in their original
order, and CPU shadow register/dirty-bit changes retain intermediate writes.
No draw, fence, IRQ, visibility decision or Vulkan state is skipped. The first
10,000 actual packets compare every index, the entire register and dirty arrays,
and final index count with the real scalar executor; verification retains the
scalar result and permanently falls back on any mismatch.

The isolated expansion test passes 163,904 cases, including every packet length
through 2,047 words, input/output alignments, unsigned extrema, sentinel bytes,
and protected pages at input/output ends. The 47,260 register-update cases,
21 real decoder boundary cases and 768-pixel Vulkan test also pass. Enable
`NFSU2_PCVR_INDEX_PACKETS_COMPARE=1` and create `index-packets-compare.start`
after verification and visual acceptance. PCVR crossovers now use 24 warmup
plus 384 measured frames per phase (eight ABBA/BAAB phases); the trigger can be
used again without restarting. The original Quest comparison lengths remain
unchanged. At a 72-Hz runtime cap, equal average FPS can conceal extra CPU
headroom; retain frame-time and draw-count evidence rather than claiming an
uncapped speedup.

The user confirmed correct menu/cars/world with packed index packets. All
10,000 live comparisons passed. At 72 Hz, the crossover under
`reports/pcvr-index-packets-20261014-202400/` measured 71.39 FPS reference and
71.73 FPS optimized, with both paths usually at the refresh cap and occasional
77–92 ms waits inside XR end. No reliable FPS improvement was demonstrated;
the experiment remains off both live and in the preset. The following driving
capture (`reports/pcvr-focus-20261014-202547/`) recorded 3,218 frames over
45.006 seconds: 71.50 FPS, p95 15.50 ms, p99 17.53 ms, worst 32.48 ms. Eleven
intervals exceeded 1.5 display periods and two exceeded two periods. These are
game-frame timing counts, not measured compositor deadline misses; the run
does not establish completely steady 72 FPS.

The active PC network adapter reports 100 Mbps (Realtek USB FE). A separate
12-second adapter-counter sample (`reports/pcvr-network-20261014-202726.csv`)
measured 47.36 Mbps mean transmit, 56.86 Mbps peak over approximately 100-ms
windows, and no reported send-error/discard increase. This sample does not
establish network saturation or measure the headset Wi-Fi/decoder path. No
network, codec, buffering, registry or system power settings were changed.

New benchmark CSVs retain the original frame/time columns and add draw,
fence, XR begin/end and statistics wall times. Sampling uses differences of
existing cumulative renderer clocks at exactly the same boundaries, with no
extra timer queries or file writes during capture. The analyzer remains
compatible with older captures and reports the slowest frames with their
phase breakdown. Residual time includes command decoding, guest/scheduling
waits and other uninstrumented work; XR begin includes ordinary pacing. These
are not GPU execution or decoder measurements. `--refresh-hz 72` additionally
counts intervals above 1.5 and two display periods, without replacing the
strict budget metrics or concealing jitter.

The first completed phase capture (`reports/pcvr-focus-20261014-203002/`)
contains 3,232 frames over 45.007 seconds at 72 Hz: 71.81 FPS, p95 15.17 ms,
p99 16.03 ms, worst 104.09 ms, with four intervals above 1.5 display periods.
The worst frame contains 94.36 ms in XR end and 3.46 ms draw CPU time. Three
other 21–27 ms intervals contain approximately 8–14 ms draw time. All rows
have finite phase values and nonnegative residual time. The route varies, so
this is not a controlled speedup versus the previous driving capture. Stable
72 FPS is still unproven. The next proposed isolated comparison disables the
previously reported Video buffering option in the Quest Virtual Desktop UI;
the user must perform that setting change. No codec, bitrate, quality or VDXR
registry setting has been changed by the agent.

The user disabled Video buffering, retaining 72 FPS. With normal process
priority, `reports/pcvr-focus-20261014-203236/` measured 71.966 FPS over 45.007
seconds, p99 16.00 ms and worst 31.18 ms, with three intervals above 1.5 periods.
The prior 104-ms event did not recur in this sample; this is promising but
does not prove causality or a permanent fix. Two approximately 28-ms intervals
contained 15–16 ms draw time; the longest interval contained 22.43 ms XR end.
The frame intervals still have outliers, so the stable-72 requirement remains
open.

A separate exploratory driving capture temporarily set only the game process
to AboveNormal priority (`reports/pcvr-focus-20261014-203530/priority.json`).
It measured 71.973 FPS, worst 32.08 ms, again with three intervals over 1.5
periods. No reliable benefit was shown; the original Normal priority was
restored in `finally` and verified. No persistent priority or power setting
was installed.

Latest CSVs also carry existing per-frame shader compilation, texture upload,
pipeline creation and cache-write counters plus draw count. They add no timer
queries. These work counters may be nested inside renderer phases, so the
analyzer displays them separately and never adds them to the phase residual.
The completed capture (`reports/pcvr-focus-20261014-203833/`) contains 3,239
frames over 45.002 seconds: 71.975 FPS, p99 16.40 ms and worst 25.10 ms.
Four intervals exceed 1.5 display periods; none exceeds two. All four longest
intervals contain 4.76–8.13 ms of texture upload work, with no shader compile,
pipeline creation or cache write. This narrows the remaining short render
stalls to the texture path but does not establish stable 72 FPS. The opt-in
`NFSU2_PCVR_TEXTURE_PROFILE=1` splits actual texture misses into image setup,
fill/upload and cache accounting, reporting format and dimensions of the
slowest texture. Ordinary cache hits and rendering behavior are unchanged.

The texture diagnostic (`reports/pcvr-textures-20261014-204251/`) found repeated
64x512 indexed uploads taking about 2 ms, and 128x512 uploads around 4 ms,
mostly in CPU unswizzling. Some separate bursts spend 3–5 ms creating many
images; texture accounting is negligible. This does not implicate GPU copy
bandwidth: the measurements are CPU wall times.

`NFSU2_PCVR_TEXTURE_DECODE=1` replaces per-pixel Morton address reconstruction
for formats 0x06, 0x07 and 0x0B with the existing D3D8 masked-increment address
walk. The Vulkan PCVR path alone uses it. It preserves alpha fill, all four
palette lengths and out-of-range palette-index fallback. Unsupported formats
or dimensions retain the original decoder. Content hashes, uploads, image
lifetime, visibility and resolution do not change. `pcvr_texture_decode_test`
compares 92,252,156 pixels in 822 cases with the original address algorithm,
including rectangular/one-dimensional images through 4096 pixels per axis,
unaligned source/palette data, sentinels and protected end pages: zero failures.
The first 64 live images of each supported format additionally compare every
pixel from one immutable snapshot; any mismatch restores the original path.
The user confirmed correct graphics. Live checks completed for 64 indexed
and 64 ARGB images with no pixel differences: total CPU conversion time was
107.875 versus 2.049 ms for indexed data, and 34.988 versus 0.519 ms for ARGB.
These are local conversion timings, not whole-game speedups. The decoder is
enabled in the preset. The following driving capture
(`reports/pcvr-focus-20261014-204648/`) averaged 71.962 FPS, p99 16.31 ms, worst
24.75 ms, with five intervals over 1.5 periods and none over two. The route
varies; this does not demonstrate improved overall frame-time tails. Remaining
texture-related spikes spend 4.6–5.7 ms creating images, versus 0.1–0.3 ms
filling them. Stable 72 FPS remains unproven.

`NFSU2_PCVR_TEXTURE_SLOTS=2048` is the next isolated cache-capacity comparison.
The previous 1,024-entry cap evicts textures even when their total contents
occupy only approximately 20 MB of the existing 256-MB budget. PCVR can now
retain 2,048 entries if the device permits at least 2,560 allocations, leaving
headroom for surfaces and deferred destruction. The byte budget, content
hashes, per-frame validation, upload ordering and image lifetimes are unchanged.
The native Quest limit stays at 1,024. Default capacity remains 1,024 pending
the larger-cache comparison. The GPU reported 958/4096 MB used before this
change; that is whole-device memory use, not a texture allocation measurement.

The user confirmed correct graphics with 2,048 slots. The consecutive captures
`reports/pcvr-focus-20261014-205009/` and `...-205118/` measured 69.708 and
71.503 FPS, with worst intervals of 106.66 and 105.23 ms. Those severe events
contain 86–94 ms in XR end and zero texture upload time. New texture allocation
bursts still occur; reported slow texture frames show zero evictions. These
captures do not establish an overall FPS gain from the larger cache, so the
preset stays at 1,024 while the live comparison uses 2,048. The decoder remains
enabled on the strength of identical pixels and its measured conversion gain.
An isolated GPU snapshot reported 87 C with software thermal slowdown active,
997 MB memory used and 42% GPU utilization; it does not prove the source of the
XR stalls. No clock, fan, power or runtime registry settings were changed.
The user has been asked for concurrent VD Game/Encoding/Decoding timings.
Stable 72 FPS remains unmet, including with Video buffering disabled.
