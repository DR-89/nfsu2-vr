# CopChase native adaptation

Work in progress, 2026-11-09. CopChase v0.3.2 by **Vitorini Mods** is being
adapted for the native Xbox-based PCVR/Quest port. The Windows ASI cannot be
loaded by either native target. This work reconstructs verified behavior from
the user's private package; it does not install the mod in the original PC
game, redistribute its assets, or claim to be its original source code.

The native offline adapter, state, steering, tactics, driver recovery,
collision accounting, hiding, VR HUD, radio/music, persistent records,
fine presentation, roadblocks and visible spike strips are implemented.
The startup VR setting now mounts a verified private asset pack; diagnostic
pack variants still require explicit scripted QA. These changes are under
integrated QA. The previously built normal mirror release is preserved
until the new combined build is ready; source completion is not a claim of
headset acceptance or complete feature parity with the original PC plugin.

## Evidence

The private input ZIP SHA256 is
`f7d68dc92dd2cb78d7dcc3e09e1e17b1444156cd8d674c0e3c72997e5d984f8d`;
the ASI SHA256 is
`43590d849ba616253e0e975269f7db62537c74013d4f2177fe179ffa0d24ea9e`.
REA 6.7.0 and Ghidra 12.1.4 analyzed the ASI without executing or patching it.
Private evidence, disassembly and normalized pseudocode are in
`build/copchase-re/`. The main-loop evidence identity is
`ev_fff1d19a41394ccf8e506890d411483a69bb2c1736043f9557a1cd93dc6a1061`.
Private generated outputs stay under ignored `build/`; no game/plugin bytes
are embedded in these portable modules.

Important entry points are `100315f0` (main loop), `10019a60` (driver hook),
`10026bc0` (steering), `10003250` (aggression), `1001f150` (heat profiles),
`10013f90` (cooldown), and `10013dd0` (pursuit termination). Decompiled
pointer types are not authoritative: timestamp additions in float-pointer
expressions need the original four-byte scaling. Recovered addresses and
ABIs were checked separately against the Xbox binary.

## Portable logic

`src/community/copchase_state.{h,cpp}` provides deterministic idle, pursuit
and cooldown phases, start/reacquisition/escape/arrest events, heat, bounty
and timing. It accepts complete observations and stable world/player
generations. It never dereferences guest pointers or mutates saves/cash.
Unknown sight or incomplete scans cannot start, reacquire, arrest or evade.
Invalid context cancels before the pause gate. Time steps are bounded and
split at 0.1 seconds; pause and invalid time cannot fast-forward a pursuit.

The recovered heat profiles use 4/5/7/8/10 chasers, cooldown durations
20/45/75/90/120 seconds and bust speed thresholds 9/12/19/20/25 km/h. Hiding
adds a 4/5/6/7/8 bonus, producing 5x through 9x progress, despite the package
README's simplified 3x description. The source's LOS-disabled 70-metre
fallback is not substituted for unknown visibility in the native adapter.

`src/community/copchase_steering.{h,cpp}` provides target XY, desired speed
and road-path selection for the four presets, roles and formations. It
handles stationary containment, catch-up, aggression, personality and turn
speed limits. Random acquisition is explicit and replayable. It does not
spawn, teleport, assign physics or write control objects.

An original-binary oracle executes ASI `10026bc0` and its math/profile
callees under Unicorn. Only GetTickCount is stubbed; the CRT feature mask
selects the existing SSE math fallback. 7,408 cases passed against the
native planner, with exact road-path decisions and maximum float error
0.00006103515625. This proves those planner cases, not a complete pursuit.
The contact timestamp side effect must be supplied by the adapter before
planning; feeding an old timestamp is not an equivalent call.

`copchase_contact.{h,cpp}` implements that side effect with strict distance,
closing-speed and elapsed-time boundaries, float32 rounding and unsigned
millisecond wrap. It uses the same filtered player velocity as the planner.
1,049 original-binary comparisons, including 208 contacts, matched the
updated timestamp and diagnostic counter behavior exactly. This estimate
is not a physical collision callback and does not apply damage or impulses.
The per-car timestamp is also used by the driver's recovery guard.

## Xbox integration probe

`src/community/copchase_probe.inl` is a diagnostic, not the gameplay feature.
It only selects a live, already indexed, nearby mixed8 traffic car in an
isolated offline career/Quick Race fixture. It checks World membership,
record/driver/control identities and reciprocal references before access.
The native 22-entry physical traffic limit is preserved; the online visual
fleet limit is unrelated.

Verified Xbox calls:

| Operation | Xbox address | Contract |
|---|---|---|
| Pursue a car | `37550` | thiscall(driver, target), provider type 5 |
| Offset pursuit | `375f0` | thiscall(driver, target, offset A, offset B, duration) |
| Return to traffic | `39b00` | thiscall(driver, 7), destroys old behaviors safely |
| Choose natural physics | `1e47d0` | cdecl(car), traffic returns 2 |
| Set physics | `1e4a70` | thiscall(car, mode) |
| Driver update | `30450` | thiscall(control, dt) |
| Heading calculation | `15520` | thiscall(control, three output pointers) |

The final offset-pursuit float is a duration, not height. Native path
calculation overwrites target XY during Update; the heading hook reapplies
the checked target before angle calculation. Control steering is an angle
in 65536 units per revolution, not a normalized analog axis.

The probe activates one car for 20 simulated seconds, then resets behavior
7 and natural physics 2 at a frame boundary. Changed identities are
abandoned without stale writes. A real PCVR QA run observed 599 update and
heading calls over 20.002 seconds, movement of about 43 metres, verified
release, and another three seconds of ordinary traffic behavior. Mocked
fixtures additionally cover world/control replacement, pause, online gates
and invalid data. Neither proves the complete mod or Quest hardware use.

## Asset loading fix

See [copchase-assets.md](copchase-assets.md) for conversion, hashes, exact
mounting and cache isolation. All 75 converted solids and the private
mixed8 archive passed structural checks. Original Xbox JDLZ machine code
decoded 574 unique prepared texture blocks (14,411,152 bytes) identically
to the independent decoder, without changing output guards. The exact
generated C decoder also passed 2,296 register-pattern cases.

The corruption cause was a mismatched shared-texture index embedded inside
GLOBAL/GLOBALMEMORYFILE.BIN. The old index requested 1,124 compressed bytes
and allocated 11,064 raw bytes at an offset that the rebuilt archive used
for a different 3,032-byte/21,944-byte block. This was an allocation/read
contract mismatch, not invalid new geometry or decoder arithmetic.

The importer now composes the rebuilt shared-texture prefix into both plain
and restored-soundtrack global metadata, updates its full file size, and
keeps every unrelated metadata/MPF byte unchanged. Cache namespaces include
the pack revision; redirects reject traversal and symbolic-link escapes.

The fixed full-pack PCVR run loaded all 38 observed shared-texture blocks
byte-exactly. All 108 recognized inputs among 119 decoder captures matched;
the remaining 11 were unchanged frontend inputs. It completed the 20.015s
one-car drive (599 updates), release and 3.002s ordinary-traffic observation,
then remained running for several minutes without the previous corruption.
Private proof: `build/copchase-re/decode-fixed/comparison.json` and
`build/xyz-session-qa/copchase-fixed-runtime.log`.

## Native integration

`copchase_game.{h,cpp}` owns at most ten existing indexed mixed8 traffic cars.
It only operates during offline career free roam. Native visibility queries
return unknown for missing streamed geometry; no synthetic sight is inferred.
Role/formation scheduling matched 2,600 original-binary cases. Synthetic
adapter tests cover context loss, pause/loading, lifecycle/reused addresses,
failed release, driver/path overrides, and clear/blocked/unknown topology.

`copchase_bridge.inl` supplies validated guest memory, an allowlisted native
call bridge preserving the complete guest register state, independent aligned
scratch, and current context snapshots. World constructors/destructors
1ef3f0/1ef650, Player 1e18d0/1e1f50 and Car destructor 1e48b0 invalidate
identities before storage is reused. Player-only changes retain the World
so surviving owned traffic can be released at the next safe boundary.
Native lifecycle disassembly and SHA256s are retained in private
`build/copchase-re/xbox/lifecycle.asm`.

The VR menu supplies enable/restart, four styles, heat1–5, aggression50–200%
and maximum1–10 chasers. This physical traffic limit is separate from the
online visual-car setting. The HUD sits above the adjustable map and shows
heat, police count, arrest/cooldown progress, bounty and terminal result.
The settings and geometry tests pass; headset acceptance remains outstanding.

## Records, incidents and results

The raw collision hook runs before Xbox `1ab4c0` normalizes an impulse. It
captures bounded, verified identity events; it never substitutes the separate
visual-damage magnitude. The incident module implements the original five
infractions, distinct damaged/taken-out cops, scalar cop damage, ram triggers,
rollover scoring and cost-to-state accounting. Six thousand original machine
code comparisons matched. The native marker collector resolves the current
HUD marker list afresh and rejects corrupt/stale lists; valid types 6–18
provide the original hiding multiplier.

Verified save-load provenance selects external `CopChaseRecords` files.
No original save is rewritten by this worker. Durable pursuit tokens and an
ordered coordinator prevent queue backpressure from losing cancellation,
settling the wrong profile or duplicating a result. Pending writes freeze
CopChase progression until the current heat is available. Cash changes use
the independently mapped native getter/add functions only after an explicit
VR confirmation in an owned root pause. An uncertain write cannot be retried.
Failures explain whether payment is known and return control to the ordinary
pause menu without an automatic second charge. Details are in
[copchase-record-flow.md](copchase-record-flow.md) and
[copchase-bust.md](copchase-bust.md).

## Roadblocks and rendering

Roadblocks use checked native path points and an atomic 13-range placement
transaction. The game adapter holds and stages selected existing traffic,
then releases it through the ordinary traffic API. A spike strip can damage
tyres only while the renderer reports that its world representation is
available. Tyre pop and rim scrape are independent audio voices.

The strip overlay uses the tracked view and the same verified world depth
attachment as the car/world render. It rejects stale frames, foreign eyes,
recycled Vulkan images/views and cleared depth/color proofs. Actual RTX 3050
readbacks cover 20 fixtures in both sequential-eye and multiview rendering:
visible geometry, parallax, partial/full occlusion and unchanged background
passed; each eye is pixel-identical between rendering modes. No Vulkan
validation layer was available. Private evidence is in
`build/copchase-re/scene-qa/gpu-results.json`.

## Remaining original-plugin differences

The adapter currently supports offline career free roam and a mixed population
of eight privately converted police vehicle slots. Heat-based vehicle resource
replacement, the original Quick Race Pursuit entry, Cross/high-heat special
units, arrest replay, jail markers, police minimap blips/health bars and the
original complete result-screen layout are separate integration work. Their
absence must not be represented as a complete installation of the Windows
plugin. Existing online visual-fleet limits do not enlarge the native physical
traffic table. Headset driving, mirror appearance, audio and final controller
acceptance remain user tests.
