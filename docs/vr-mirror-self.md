# Rear-view mirrors: road and traffic

The interior/self-car capture described below was removed on 2026-11-10 at the
user's request after headset testing. Current PCVR, Quest and Web mirrors use
the retail rear-camera position and exclude the local car. There is no added
cabin render pass in the mirror target. Glass trim affects only the adjusted
mirror's rays, never the shared source camera.

A live native capture identified the remaining dark rounded bar as the retail
RVM/RVM_MASK overlay. The renderer resolves their current TextureInfo pixel
addresses and omits these overlays only in the physical rear capture. The
Web renderer already filters the corresponding retail textures.

The following sections document the superseded self-reflection experiment.

Implemented for the Vulkan PCVR and Quest renderer on 2026-11-09.

The native rear-camera capture now includes the player's rigid body shell.
Only this rear capture relaxes the normal cockpit camera's own-car exclusion.
The regular driving camera retains its existing exclusion behavior. Rear
capture matching checks the camera, player mover, rigid transform and depth
convention; other cars and wheel transforms cannot be mistaken for the
player's shell.

During the rear capture, the player's shell can be seen from inside. The
player's window surfaces and crossed driver cards are filtered for this
capture. The fitted rear interior is drawn into the same rear color/depth
target, so opaque scene geometry still occludes it. Hands, HUD and mirror
surfaces are not recursively included. The capture uses 146.25-degree field
of view with near/far planes 0.18/600 metres.

Source changes are in `android/runtime/quest_mirrors.h`, the camera wrapper
in `src/recomp_manual.c`, `xboxrecomp/src/nv2a_vk/nv2a_vk.c`, and
`xboxrecomp/src/nv2a_vk/quest_cockpit_vk.inc`. No OpenGL rendering path was
changed for this feature.

## Verification and artifacts

Private evidence is in `build/mirror-self-qa/`:

- Transform tests cover 29 fitted vehicles and five rotations, invalid
  transforms, other vehicles, wheel matrices and nonfinite values.
- Seven actual camera-wrapper scenarios pass, including ordinary camera
  restoration and wrong-camera rejection.
- The production Vulkan rear-interior injection draws 78,040 cabin pixels
  while preserving 497,960 background pixels and an existing depth occluder.
  Stale/foreign captures are rejected; mono and array-target results agree.
- Twelve stereo mirror-shader cases, 512 pixels each, pass.
- PCVR and ARM64 builds and the Quest APK packaging check pass.

These are automatic renderer and integration checks. A headset check of
mirror position, perceived depth and the rear cabin remains a user test;
it is not inferred from the automated results.

The normal PCVR executable at this checkpoint is
`build/pcvr/game/nfsu2-pcvr.exe`, SHA256
`80797305ee09226c798892eac778733c8621ece325407776d2e22dced1a94526`.
The Quest APK is `android/app/build/outputs/apk/debug/app-debug.apk`, SHA256
`b2d09672e44ce579b327e5c7c2ae9abefc53da8fa96084549b93052ca76d3604`.
The APK was built, not installed on the headset during this work.
`release.json` records the artifact checks and unchanged user-save/profile
hashes at packaging time. CopChase uses a separate scripted diagnostic
executable and is not enabled in this mirror release.

## Mirror artifact correction (2026-11-10)

The rear camera clips behind the discovered mirror assemblies. Its near plane
is computed from the fresh, fitted mirror bounds and the actual rear-camera
axis after positioning the reflected eye. This attempted to clip opaque mirror backs; the user subsequently confirmed
that the dark bar remained. A live capture identified the retail 2D overlay
as another source. In that experimental implementation, a missing/stale mount retains the 0.18 m default.
The capture far plane remains 600 m, and the rear seats/body remain included.

The Vulkan and WebGPU mirror shaders reject silhouette crossings that do not
intersect a real surface. Sampling density follows ray length in source texels;
refined intersections must reproject within 0.75 texels. This removes the false
surface fragments without removing stereo parallax. The analytic regression
includes a narrow nearby post with a visible background behind its silhouette.

Verification in `build/mirror-fix-qa/` includes the actual camera wrapper,
all-car mirror geometry/depth tests, camera lifetime tests, and 13 Vulkan stereo
cases (512 checked pixels each). PCVR and Quest ARM64 compile. The normal Quest
mirror package excludes the unfinished CopChase asset directories via the
private Gradle packaging init script. No headset installation or optical test
was performed. The Web port's real-game before/after captures show removal of
the speckled edge pattern and report zero GPU errors.

The native full build also required a two-line compile repair in
`copchase_game.cpp`: the local pursuit visibility flag is kept local instead of
accessing a nonexistent `CopChaseInput` member. This does not enable CopChase.
The working tree's other in-progress native code is retained.

## Movable screen depth

The screen is blended after the wheel, map and hands, with the completed cabin
depth loaded and the unmodified cabin eye matrices rebound. It keeps its
non-writing alpha pipeline: a nearer screen covers the wheel and a farther
screen is occluded by it, including partial opacity. GPU readback in
`build/screen-depth-qa/` checks the production screen shader at 0/35/100%
opacity; all 512 stereo pixels pass. Replaying the previous order fails 256
foreground pixels. PCVR/Quest builds and interaction tests pass. The Quest
APK is built, not installed; an unavailable VirtualDesktopXR headset prevents
a new physical-headset check at this checkpoint.
