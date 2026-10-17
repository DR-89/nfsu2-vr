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
The runtime uses the binary asset; JSON describes its baseline preparation.
`android/runtime/quest_cockpit_vehicle.h` supplies the per-vehicle eye and cabin
fit. Future cabins can retain the same driver-eye origin and binary layout.

The Peugeot reference fit places the guest eye at forward/left/up `(-0.10, 0.35, 0.95)`
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
The accepted steering travel is retained. Other cameras keep their original
vehicle rendering.

Cockpit View omits the player's original window glass, including custom tint
and the rear defroster layer, so the retained shell cannot darken the view.
`quest_cockpit_windows.h` resolves the seven window-material replacement slots
from the current player's live `CarRenderInfo`. Addresses are refreshed on the
guest thread and copied with the vehicle profile once per stereo frame. Only
the player's depth-tested transparent window draws in the main cockpit world
pass are omitted. Body, window frames, original mirror glass, other cars,
reflection cameras and exterior views retain their normal rendering. Invalid
or missing texture records retain the original draws instead of using stale
texture addresses; no game textures or save data are modified.

The stock-material audit finds 267 window submeshes across all 29 vehicles,
all using these seven semantic texture slots. Windows and Quest tests cover
live texture replacement/reload, invalid records and 6,417 captured draw calls
from Peugeot, Pontiac and Lancer scenes: 17 player-window draws are omitted,
with other draws retained. The retail capture fixtures stay in ignored
`build/` directories. This is automated coverage, not visual acceptance of
every tint/body-kit combination. The user subsequently confirmed clear cockpit
visibility, correct mirrors and retained exterior tint on native Quest.

### Vehicle-specific fitting

All 29 stock cars have individual base eye positions measured from their
retail driver geometry. The accepted Peugeot eye-to-driver offset is retained;
windshield depth and body width fit the static dashboard and door trim to each
shell. The Audi A3 and Skyline use the driver side present in their stock assets.
The Miata eye is about 21 cm farther back and 14 cm lower than the old universal
position. Its dashboard is shortened to fit behind its windshield. Miata and
350Z omit the rear bench and rear trim, retaining both complete front backrests.
The source-space rear cutoff is 46 cm behind the base eye: front backrests end
at 42.5 cm and the rear cushion begins at 50 cm.

The original exterior, hood, roof and mirror glass remain vehicle geometry.
The wheel and controller hands keep their physical dimensions and grasp pivot;
the dashboard gauge and map follow the fitted static cabin. Personal Seat Height
and Seat Distance settings still adjust the eyes together with all rendering.
The common cabin is a fitted prototype, not a recreation of each car's interior.

Mirror detection accepts the slightly curved glass used by several cars and the
Hummer's glass embedded in a larger material batch. The current geometry audit
finds both side mirrors for all 29 stock cars and no added-cabin obstruction of
their centers from the base eye. This does not certify every mirror pixel,
aftermarket part, personal seat offset or moving-head view.

The measurement derivation is reproducible using local game data only:

```text
python tools/audit-cockpit-cars.py game build/cockpit-audit
python tools/audit-cockpit-driver-cards.py game build/cockpit-audit/stock-solids.json build/cockpit-audit
python tools/audit-cockpit-fit.py build/cockpit-audit/stock-solids.json build/cockpit-audit build/cockpit-fit
```

The last command checks the compiled profiles against the measured stock
geometry without overwriting them. All extracted fixtures remain in ignored
`build/` directories. The cockpit tests also cover all 29 body/driver/central
mirror fixtures, both side mirrors and profile alignment on Windows and Quest.

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
existing race HUD supplies the live instrument overlay behind the wheel.
The gauge plane is at cabin `(0, -0.205, -0.66)` meters, 10 cm deeper than
the previous placement and close to the shared model's instrument face.
Its 24 cm size and vertical position are retained. The overlay stays visible
through the baked dashboard trim, while the wheel and hands render over it.
The user confirmed this deeper placement fits the instruments and the display
remains fully legible on Quest.

The map is mounted on the center stack over the radio/heater controls, at
reference cabin `(0.3116, -0.30, -0.405)` meters, sized 20 by 22 cm. Its top
leans away with the console face. The vehicle fit moves it with the dashboard
and places it on the appropriate side in right-hand-drive cabins. Like the
gauge, it stays legible over the baked console trim; wheel and hands draw over
both instruments. Both panels share one draw. Pause menus still use one
complete, unsplit surface.

Cockpit View is inserted at the wrap of the normal driving camera cycle,
after the four usual views (the extra retail view in special modes is retained).
The `sub_001E32E0` hook handles the driving-input caller `0x001F2C6E`; it does
not modify the separate cinematic camera selector. The saved retail camera
index remains valid. Right stick click cycles cameras. Left stick click holds
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
The later vehicle fitting is geometrically checked across the stock fleet;
it has not been visually accepted in every car. No PCVR cockpit acceptance or
stable 72 FPS result has been established for this fitting change.
