# Shared cockpit prototype

The first cabin is shared by all cars. All cabin geometry and color textures
come from one model generated privately in the user's Tripo Studio Chrome
session on 2026-10-15: dashboard, steering wheel, console, both front seats,
rear bench, rear shelf, floor and door interiors. Its palette
is anthracite with blue fabric accents and silver trim. The previous prototype's
procedural rear cabin is no longer used. No original game assets are included.

Tripo project: `bdc7cbdd-2a2f-417f-ab90-78152027aeb6`.
Textured result: `c3fc9baa-0f8c-47fe-8f6f-d03200a20fd2`.
Generation: Tripo image reference, H3.1 Best Quality, Generate in Parts Detailed,
then 4K texturing; 60 existing credits for this geometry and 20 for its texture.
Earlier rejected geometry attempts are not used. The project was not published.
The original output is `source/tripo-full-cabin-textured.glb`.

The raw generated model required fitting. `source/full-cabin-fit.json` records
the driver-eye transform, omitted headliner, mirrored existing door/window trim,
opaque glass removal, duplicate window-frame removal and steering-column split. Every retained or mirrored
part still comes from this same Tripo cabin; no replacement cabin geometry is
created procedurally. It remains a generated prototype, with imperfect trim
and baked instrument detail rather than a finished vehicle-specific interior.

Preparation (Python dependencies in `tools/cockpit-requirements.txt`):

```text
gltf-transform copy assets/cockpits/source/tripo-full-cabin-textured.glb decoded.glb
gltf-transform simplify decoded.glb simplified.glb --ratio .55 --error .003
python tools/prepare-cockpit.py simplified.glb assets/cockpits/runtime/cockpits/shared.ncpk --preview assets/cockpits/shared-preview.glb --config assets/cockpits/source/full-cabin-fit.json
```

glTF Transform 4.5.1 was used to decode the original meshopt geometry and
simplify it. The runtime has 50,410 triangles, one 4096-square RGBA atlas, and
two draw ranges. Tripo parts 32 and 48 supply the steering rim and hub; part 32's
column stays fixed. Their common pivot/shaft are fitted from the rim. They
rotate together by up to 180 degrees in either direction (360 degrees total), following the actual
player-one left-stick X value delivered by the input backend. A 45 ms exponential
filter smooths the visual motion independently of frame rate. This is an input
animation, not a reading of the game's physical front-wheel angle.

`runtime/cockpits/shared.json` contains geometry metadata and the seat offset.
The current runtime uses the binary asset; JSON is descriptive, not yet a
per-car configuration loader. Future cabins can retain the same driver-eye
origin and binary layout, with an explicit vehicle-to-cabin mapping added later.

The current fit places the guest eye at forward/left/up `(-0.10, 0.35, 0.95)`
meters from the car origin: 8 cm closer and 10 cm lower than the preceding
fit. The asset translates by OpenXR `(0, +0.10, +0.08)` meters, preserving
the cabin's placement relative to the original body while moving the eye.
Tacho and map receive the same translation. The wheel center is approximately
33 cm forward and 21 cm below the eye. The user confirmed this fit on Quest.
The shared model's headliner and attached roof trim (parts 1, 3, 84, 90 and 96)
are excluded at the user's request, removing 5,915 triangles. The original
source is retained so the removal can be reversed.
The main cockpit camera retains the original player
body and hood. To avoid duplicate window frames, Tripo parts 13, 21, 31, 43,
46, 64 and 71 (including their mirrored copies) are omitted: the retail car
supplies the pillars, and the Tripo model supplies the interior below them.
The accepted steering travel is retained. This shared fit
still needs evaluation against each vehicle's shell. Other cameras keep their original
vehicle rendering.

NCPK v2 stores a 64-byte little-endian header: magic, version, vertex/index
counts, atlas width/height, wheel first index/count, pivot float3, normalized
shaft float3, two reserved zero uint32s. Interleaved position/normal/UV float
vertices, uint32 triangle indices and raw RGBA atlas follow. The parser checks
all sizes, ranges, finite values, index bounds and the shaft before GPU upload.

The native Vulkan overlay uses its own near plane and depth attachment. Both
eyes use the same wheel angle; only camera matrices change each frame. The
original cameras and screen-space culling setting are preserved. The cockpit
world uses an 8-cm near plane to retain the close retail roof/window frames,
with the corresponding depth-offset correction; other driving cameras retain
their original 0.5-unit near plane. The cockpit replaces only the live player driving camera, not
the race introduction or menu cameras. Translucent controller-driven ghost hands
are added by the Vulkan renderer only in Cockpit View. They use the separate
Tripo glove asset described below; the cabin geometry is unchanged. Grips near the rim enable
one- or two-hand motion steering; the wheel still follows the steering value
delivered to the game. Functional dashboard needles are not implemented; the
existing race HUD is moved above the dashboard.

Cockpit View is inserted at the wrap of the normal driving camera cycle,
after the four usual views (the extra retail view in special modes is retained).
The `sub_001E32E0` hook handles the driving-input caller `0x001F2C6E`; it does
not modify the separate cinematic camera selector. The saved retail camera
index remains valid. Left stick click cycles cameras. Right stick click holds
the original rear view outside Cockpit View and is ignored inside it; physically
turn your head to see the rear cabin. Both stick clicks recenter and release grips.
Set `NFSU2_VR_COCKPIT=0` to disable loading the feature.

### Tripo driver gloves

`source/tripo-racing-glove.glb` was generated with the Tripo MCP text-to-model
tool on 2026-10-15, task `5e8ee832-072e-42f9-8edf-a4566c71d52b`, face limit 6,000.
It supplies one right racing glove with blue fabric and dark palm/padding.
The left glove mirrors the same source. No external logos or game assets are used.
`tools/prepare-gloves.py` applies metric fitting and bakes relaxed/gripping
poses with a continuous finger curl and thumb rotation. The 5,980-triangle
source geometry and texture remain; the texture is reduced to 1024 square.

```text
python tools/prepare-gloves.py assets/cockpits/source/tripo-racing-glove.glb assets/cockpits/runtime/cockpits/driver-gloves.nghv --preview assets/cockpits/gloves-preview.glb
```

NGHV v1 has a 32-byte little-endian header (magic, version, vertex count per
pose, width, height, three reserved zeros), two nonindexed float8 PNUV poses,
then RGBA8 pixels. Runtime checks bounds, finite positions/normals, UVs and
exact byte length before GPU upload. Both poses share one texture and require
only one draw per visible hand (one draw also covers both multiview eyes).
The shader blends the gloves at 68% opacity. Missing/invalid gloves use the
earlier procedural ghost mesh as a fallback. The optional glove file is bundled
with the APK and copied beside the PCVR executable with the cabin.

Grasping is allowed at any point around the rim, independently for each hand,
including left hand on the right and right hand on the left. A fresh grip uses
the current wheel angle; releasing one hand does not reset steering. Grip tests
cover 24 crossed-hand positions, transfers and separate contact locations.

Camera/control/steering math and malformed asset tests are in
`tests/quest_cockpit_test.c` and pass on Quest and Windows. On 2026-10-15 the user
confirmed the earlier prototype's cockpit switch and animated wheel in a native
Quest race, after disabling Vulkan validation. The replacement full cabin passes
Quest and Windows parsing/math tests and has been inspected in a textured 3D
preview. After correcting the hook from the director to the actual driving
camera event, the user confirmed the new camera cycle, full interior and moving
wheel together on native Quest: "Kamerawechsel, Innenraum und Lenkrad funktionieren".
No vehicle-by-vehicle seat fitting, PCVR cockpit acceptance or stable 72 FPS
result has been established yet.
