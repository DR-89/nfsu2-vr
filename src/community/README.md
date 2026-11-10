# Native nfsu2.xyz support

Shared C/C++ implementation for PCVR and Quest 3. The profile UI is wired into
VR Settings. Explicit Connect Free Roam / Disconnect actions now drive the
worker, core and owned-car manager. Shared tests, the real service handshake
and Quest rendering of a live-service QA peer pass. Native race browsing,
joining and hosting are implemented; final browser-player gameplay and race
acceptance remain outstanding. See the complete
acceptance checklist in `docs/nfsu2-xyz-integration.md` at the repository root.

* `xyz_profiles`: up to eight persistent local connection profiles, independent
  of career saves. The device identity stays fixed across profile selection.
  Names match upstream `onlineName` and its name filter. This does not claim a
  name, create a server account, or import/export a career.
* `xyz_profile_api`: C interface owned by the OpenXR/render thread; the shared
  `quest_xyz_profiles.h` menu supports controller text entry and deletion
  confirmation. Profile writes use flushed, exclusive temporary files and
  atomic replacement; an unreadable/corrupt file must not be overwritten.
* `xyz_auth` / `xyz_auth_store` / `xyz_credentials`: existing eight-word
  keyphrase derivation, asynchronous per-profile token storage through Windows
  DPAPI or Android KeyStore, and explicit removal. The controller keyboard
  masks the phrase and clears input buffers on exit; credentials never enter
  the profile catalogue or browser snapshots. Windows integration tests use
  real DPAPI. Actual Quest KeyStore and reserved-name login need acceptance.
* `xyz_protocol`: big-endian virtual LAN framing, JSON handshake, queue/resume,
  heartbeat and session controls. Only uncompressed frames are negotiated.
  Server name/kick/ban decisions and protocol failures stop reconnect attempts.
  A claim, when provided, is the upstream 43-character base64url token, not a
  keyphrase or the server's SHA-256 of that token. Tokens are never put in URLs.
* `xyz_socket_win`: WinHTTP WebSockets with system TLS verification, redirects
  disabled, bounded message assembly and receive queue. The only plaintext
  factory is fixed to 127.0.0.1 for the deterministic local transport test.
* `xyz_socket_android`: JNI to `XyzWebSocket.java`, with Android's default TLS
  trust/hostname verification through OkHttp. Events are polled, never callbacks
  into guest memory. The package-private test constructor accepts loopback only.
* `xyz_xbox_vehicle`: read-only US Xbox vehicle adapter. It walks the bounded
  world list, verifies the player's membership, reads COM pose/control data and
  converts all 170 part slots plus performance/tuning to the website's car
  record. Invalid lists, poses, pointers and incomplete rides are rejected.
* `xyz_roam_link`: game-thread ownership of the portable core, virtual-IP and
  connection-generation binding, bounded outgoing datagrams, pause/transition
  status and peer interpolation. Tests route two real core instances through
  the website's envelope, with a synthetic hub and fragmented car records.
  Existing receive peers survive the core's bounded hub-removal grace; paused
  peers keep their last pose while control traffic keeps the link alive.
* `xyz_xbox_remote`: bounded ride reconstruction through a typed whitelist of
  Xbox calls. Resolves parts by both key and owner, reports stock fallbacks and
  missing parts, and writes only supplied owned memory.
* `xyz_xbox_clone`: asynchronous creation, equipment streaming and retirement
  of owned, unindexed cars. Preserves the stock racing index/debug arrays,
  verifies list and record ownership, reserves an unused stock paint slot and
  keeps stable ride buffers until loader references are released. CreateCar
  requires the copied original physics tables; its bootstrap stays hidden
  until the received equipment loads. Its pose
  writer checks inputs and ownership before updating COM, rotated render
  origin, wheel/caliper matrices, steering, suspension and slip; section ground
  snapping cannot overwrite the received orientation. Hide/show keeps the car
  inert and outside the indexed opponents. Shared part packs, extra paint
  textures, effects, collisions and actual world transitions remain open.
* `xyz_transport`: network-thread ownership of Session and Socket. Explicit
  start/stop commands and bounded copied datagrams are the only cross-thread
  data. Stop invalidates the game generation immediately without waiting on a
  blocked send or close. Queue, resume, profile replacement, timeout and server
  removal retain the existing protocol rules. No socket is made on boot.
* `xyz_client`: copied render commands, game-thread core ownership, queue and
  generation binding, bounded datagram exchange and peer snapshots. Local
  races/loading/menus keep the connection but never spawn free-roam peers.
  Explicit LAN sessions also own the virtual socket bank. Socket calls pump
  copied LAN queues without requiring another game frame; status snapshots
  read current worker state even while the game loop is busy. Disconnect,
  profile replacement, race restrictions and generation changes invalidate
  old sockets immediately, preserving handle tombstones until guest Close.
* `xyz_lan`: the browser's UDP and ordered-stream protocol over the existing
  WebSocket. Bounded queues, short writes, listener/accept, connect readiness,
  half-close, local loopback, peer departures and reconnect generations. It
  consumes the transport's separate LAN queue; it never opens host sockets.
* `xyz_xnet`: bounded guest-memory adapter for 17 audited Xbox socket entry
  points. It preserves stdcall cleanup, guest-thread last errors and socket
  readiness. Protocol-254 UDP's two-byte Xbox prefix is removed/inserted at
  this boundary. Guest reads, writes and fd sets are validated. Only the
  audited nonblocking path is supported; blocking select is rejected.
  The entry wrappers route explicitly enabled LAN sessions through the client.
  Retail handles keep their original implementation, with matching last-error
  ownership; stale virtual handles never fall back into Xbox internals. The
  independent isolated probe is limited to its owning guest thread. Ordinary
  Free Roam still selects `lan:0`; the race lobby/game flow remains pending.
* `xyz_fleet`: six owned-car slots for the six receive candidates,
  serialized constructor loads, independent paint, configuration replacement
  while preserving the previous visible model, stale-pose hiding and
  retirement on disconnect, peer departure or world change. Arenas are bounded
  and retained until their loader references can be safely released. Current
  and approaching distance rank candidates; a substantially nearer peer is
  required before replacing an assigned car; nearby visible cars and newly
  shown cars are protected from rank eviction. New loads wait for city streaming
  and the original car loader to be idle.
* `xyz_skins`: six private paint sets (body, wheels, spinners) at slots 7..12.
  Pixels, palettes and Xbox texture headers use independent bounded physical RAM
  through `xyz_paint_bridge.inc` (18 owned allocations outside the game heap),
  published together after validation. Original texture packs are unchanged.
  This removes the two-car cap imposed by spare stock paint slots.
* `xyz_game_api`: render/game bridge used by both platforms. Guest operations
  run only at the main-loop hook; the hook prevents loader-driven re-entry.
  Status logs contain counts and states, no identity or resume credentials.

Network ownership is deliberately separate from guest/render ownership. The
socket/session must be driven by a worker; only copied datagrams cross to the
game-thread core and vehicle adapter. Connect/close and certificate checks
must never run on the render thread. Do not instantiate the transport merely
because the game boots or a profile is created.

## Reference code and licensing

`vendor/roam/{roam_core.c,roam_core.h,names.h,roam_selftest.c}` are MIT-licensed
source from th0mk/nfsu2-web commit
`304978012da55b273c143f9a8bcec7d1d298a873`. The self-test was copied from `tests/`;
the other files from `runtime/`. Its license is alongside them. The only core
changes are an MSVC string-builtin guard and MSVC x86/x64 detection for the
same SSE square-root instruction. Compile with strict floats / FP contraction
disabled. Upstream's golden byte stream is 37,880 bytes, FNV-1a `5b4b09dc`.

`vendor/cjson/` is cJSON v1.7.19 (MIT), unchanged, from
https://github.com/DaveGamble/cJSON/releases/tag/v1.7.19 . SHA-256:

* cJSON.c: `298581a04a36c0165da4b0aade235c23088cb2faa58651d720ea2f3706ed0b0d`
* cJSON.h: `25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee`
* LICENSE: `a36dda207c36db5818729c54e7ad4e8b0c6fba847491ba64f372c1a2037b6d5c`

The parser caps input/depth, rejects duplicate object keys and trailing data,
and validates types/ranges before mutating session or profile state.

## Checks

For Windows, from an x64 Visual Studio environment:

```
cmake -S src/community -B build/xyz-native -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/xyz-native
ctest --test-dir build/xyz-native --output-on-failure
```

Nineteen checks cover the upstream byte oracle, wire/control protocol, persistent
profiles, controller menu, Xbox vehicle conversion, core/envelope integration,
ride reconstruction, owned-car lifetime/equipment changes, motion, the network
worker, complete client/core exchange, fleet scheduling, shared accessory
headers/model reference repair, virtual LAN streams, the Xbox socket boundary,
and real WinHTTP
against a local WebSocket fixture. Its final two
connections exercise the complete worker's abrupt disconnect/resume and
operator removal. The eighteen
shared native checks also pass on Quest 3 hardware
(2026-11-08), including the identical byte oracle. `XyzWebSocketTest` exercises the real Java
WebSocket bridge on the host JVM with MockWebServer. These are local tests,
not proof of interoperability with deployed website players. The separate,
opt-in `xyz_live_probe --connect` completed the real service's verified TLS
handshake on 2026-11-08, then disconnected without publishing cars or claiming
a name. It is never run by CTest.
The explicit `xyz_live_peer --connect ROOM OWNED_CONFIG SECONDS fixed|follow`
tool exchanges a captured car configuration and synthetic trajectory in a
specified room, with a temporary identity and a 10..900 second lifetime. Two
instances passed a 25-second real-service exchange on 2026-11-08 with exact
equipment and received poses in both directions. It is not a running game or
proof of browser rendering. Neither diagnostic is started automatically.

`NFSU2_XYZ_XNET_PROBE=1` selects an isolated local endpoint while calling the
original EA socket routines from the guest main loop. It creates TCP and UDP
pairs, verifies connect readiness and exact payloads (including Xbox's
protocol-254 conversion), and closes them through the original game code.
This passed in the PCVR desktop runtime on 2026-11-08; the race continued
afterward. The probe opens no service connection and never advertises a lobby.
Its 17 XNet thunks, nine EA routines and byte hashes are documented in
`config/nfsu2-xyz-xbox-map.json` under `socket_boundary`. The same portable
adapter passes native Quest tests; actual Quest guest execution and the race
lobby/game flow remain open. Keep `_gen` declarations on separate lines
in `recomp_manual.c`, as required by the generator's manual-wrapper scanner.

`xyz_live_xnet --connect` exercises the actual game C API on one endpoint and
the client directly on the other, using owned synthetic guest RAM. It passed
over the live service on 2026-11-08: 65,537 exact TCP bytes each way, both FINs,
and a 1,464-byte UDP payload with the Xbox prefix adapted on both sides. It
uses temporary identities, no name claims or saved careers, no lobby
advertisement, and a 25-second lifetime; it is excluded from CTest. The new
`xyz_session_test` covers departures, restrictions, same-IP resume, profile
replacement and disconnect while socket calls continue without game frames.
The internal `xyz_online_connect_races` entry selects the LAN-capable session;
it does not create a lobby or enable gameplay capabilities on its own.

`NFSU2_XYZ_LOBBY_PROBE=1`, with fresh 32-hex `NFSU2_XYZ_PROBE_PROFILE` and
`NFSU2_XYZ_PROBE_DEVICE` values, additionally exercises the actual retail Xbox
lobby code from an isolated Quick Race. It refuses an existing session. Two
owned clients share one TLS/LAN session and VIP; their original code performs
redirect, login, persona selection, game search and private/password-protected
create/join/ready/leave/delete. The original list holds the 20 records returned
by the service. Host-side asynchronous readiness/departure are verified too.
The diagnostic deletes its lobby, destroys the retail objects, disconnects and
has a 45-second deadline. No race start/results, chat, name claim or persisted
profile. This passed in PCVR desktop execution on 2026-11-08.
Evidence: `build/xyz-lan-qa/lobby-validation.json`; exact routine hashes:
`config/nfsu2-xyz-xbox-map.json` → `lobby_boundary`.

`xyz_xbox_lobby` and its typed guest bridge bind the normal Xbox managers,
including retail Join/Ready/Leave, assigned-persona handling, bounded input,
stale-generation cleanup and conditional restoration of the four name fields.
`NFSU2_XYZ_LOBBY_ENGINE_PROBE=1` exercises this controller from the frontend
after 20 seconds, with the same temporary identity parameters and deadline.
The real game passed on PCVR desktop and standalone Quest: list, wrong password
and retry, original two-player roster, readiness, leave/delete and cleanup.
Evidence: `build/xyz-lan-qa/lobby-engine-validation.json`. The updated Quest APK
is installed with its normal settings restored; no diagnostic starts by default.

The original play handler consumes the MADDR text field, which the PC service
leaves empty. Its owned callback supplies each validated ADDR as MADDR in an
owned copy, then calls the original handler. The diagnostic validates the
conversion of a real service roster through original GameRecord/IPv4 parsers;
it does not dispatch a race-start event. `xyz_xbox_lobby_test` also covers
partial name-write rollback, replaced ownership, malformed addresses/counts,
buffer limits, host departure and deferred rejection outside retail callbacks.
The controller also binds original Create and Start; Start requires the local
host and every other player's readiness, never the retail kick-unready option.
Server acknowledgement alone does not complete startup: the owned original
play callback must populate the race connections before PLAY_READY.
Entry now rejects active retail movies and pending frontend transitions,
including unreadable gate fields. Flow 3 alone also includes startup films;
it is not sufficient evidence that the menu is ready for a race.

`NFSU2_XYZ_RACE_LINK_PROBE=host|guest` exercises two full games with independent
TLS sessions/VIPs. Original manager Create/Join, NetGameUtil/CommUDP handshake,
and NetGameLink reliable 100-byte payloads in both directions passed on PCVR
desktop and standalone Quest. The retail utility's `mwid` is 100; a 257-byte
probe was correctly rejected before this check was corrected. Guest leave,
host delete and disconnect also passed. Evidence:
`build/xyz-lan-qa/race-link-validation.json`.
The explicit diagnostic additionally needs temporary profile/device UUIDs,
a 24-hex password and, for the guest, the host's generated lobby name.
`NFSU2_XYZ_RACE_START_PROBE=1` extends it to the original private-lobby Start,
play callback and coordinator. Both games registered two original NetGame
players, and original Stop/reset, lobby leave/delete and disconnect completed.
Evidence: `build/xyz-lan-qa/race-start-validation.json`. The online frontend
transition is original `0x1e0270`, followed by `0x1f1b80`. The client normally
waits for an Xbox Live machine-ID lookup; `xyz_xbox_lobby_resolve_virtual`
completes its exact resolver/slot/port only when the dotted address matches
the received host roster. Call it synchronously after coordinator creation,
before its next networking tick (direct lifted calls do not consult the
manual indirect-call table).

The earlier connection diagnostic used placeholder track 1001 and requested
missing L1 assets. The current fixture uses the verified offline circuit 4014.
`NFSU2_XYZ_RACE_WORLD_PROBE=1` also passes the roster names through original
NetGame setup, selects retail network mode 8, waits for a valid world/car and
then requests the original frontend return. The first fixture retained its
lobby during loading and stalled with a null streamer manager: heap 0's
largest free block was 14,973,140 bytes versus a 17,408,000-byte request.
The handoff now follows retail 0xf94e6..0xf950e: consume optional server
configuration, release the lobby with 0x19aea0, set profile state 3 through
0x113e80, then initialize NetGame. `xyz_xbox_lobby_enter_race` validates and
retains the host address and assigned persona, frees its arena, and continues
CommUDP idle processing without touching the destroyed lobby protocol.
Its detached state still rejects changed sessions or another manager owner.

With that handoff the Quest loaded Circuit with two registered players and
a valid local car, then returned to the frontend and disconnected. PCVR also
allocated the full streamer and eventually created both cars, but its first
run lost the session during loading; its race/return check did not pass.
Evidence: `build/xyz-lan-qa/quest-race-world-release.log`,
`pcvr-race-world-release.log` and `pcvr-race-world-release-final-state.json`.
The earlier failure remains in `race-world-validation.json`. Car/grid
configuration, driving and results still need validation; a loaded car alone
does not establish them. The later readiness check is recorded below.
The controller pumps original DirtySock idle callbacks on the guest thread,
as the legacy Xbox Live startup thread is not used by this session.
The loading wrapper at `0x1894f0` also pumps a detached race on its initiating
guest thread, at most every 10 ms and only in loading/unloading flows. This
does not sample cars, advance gameplay or run probe state transitions inside
the loader. A diagnostic recorded a 16.683-second outer-frame gap and LAN
departure queue overflow without this pump; WSS reconnected as generation 2
and the controller correctly rejected the stale generation-1 race.
The subsequent two-device run passed world loading and original frontend
return on both platforms without changing session generation. PCVR's
16.680-second outer-frame gap recorded 577 loader idle calls (630 by return);
Quest recorded 126. Both normal environments and the career were preserved.
Evidence: `build/xyz-lan-qa/race-world-idle-validation.json` and the matching
`pcvr-race-world-idle.log` / `quest-race-world-idle.log`. This does not verify
countdown, driving, results, browser interoperability or headset appearance.
`NFSU2_XYZ_RACE_COUNTDOWN_PROBE=1` additionally waits for the original
coordinator to reach running state 5 before returning; it never forces that
state, readiness flags or the race clock.
That check passed on both devices, including the same network start clock,
both player states becoming 2 and original frontend return/disconnect.
Evidence: `build/xyz-lan-qa/race-countdown-validation.json`. The visual
countdown and actual driving synchronization are separate checks.
`NFSU2_XYZ_RACE_MOTION_PROBE=1` extends this observation and samples original
local/remote car positions; an external USB pad script supplies throttle.
The probe itself never writes car positions or physics state.
The first motion run correctly failed: both original human drivers had input
device -1 and neither local car moved. Retail's Ready event records the
accepting logical controller at `0x406c98`; `xyz_xbox_lobby_select_controller`
performs that bounded selection before Ready and preserves it across the
race handoff. It also requires the original mapping at `0x427ff8 + index`
to be connected, matching `0x17b0e0`. USB PAD1 is not necessarily logical
index zero. A second failed motion run selected disconnected logical index
zero while index two was connected (device 65). The original game entered
pause state 6/reason 7 with a controller dialog; the local player had input
zero but the human driver remained unbound. Evidence is retained in
`build/xyz-lan-qa/pcvr-race-motion-input-map.json` and
`race-motion-validation-disconnected-logical-pad.json`. The corrected probe
chooses a connected logical index; it does not dismiss a dialog or force
the original driving state. The corrected paired runtime check passed:
both local cars accelerated through ordinary USB pad input, and the original
remote cars followed them in both directions. PCVR's local car travelled
251.00 m from its initial sample, Quest's 226.32 m. Matching independently
sampled positions by network clock gave median errors of 0.403/0.477 m and
95th-percentile errors of 1.515/1.223 m (PCVR-to-Quest/Quest-to-PCVR), across
110/117 moving matches. The original controller dialog no longer appeared,
and original return/disconnect and conditional controller restoration passed.
Evidence: `build/xyz-lan-qa/race-motion-validation.json`,
`race-motion-comparison.json` and `pcvr-race-motion-snapshots.json`.
Close or session failure restores the earlier selection only while it is
still owned. Unit checks cover invalid/disconnected indices, failed reads/writes, manager
ownership, handoff retention and conditional rollback on Windows and Quest.
This verifies bounded two-player driving between the VR builds, not a full
race finish, browser interoperability or headset appearance. Results and
player-facing lobby UI remain open.

`xyz_xbox_lobby_game` copies a displayed browser row from the owned READY
lobby. It follows the original list accessor's two indirections, bounds both
counts and pointers, and requires terminated names/parameters and consistent
player counts. The copy includes identity, host, parameters, flags and player
limits; it contains neither guest pointers nor AUTH. Failed reads clear the
result and perform no join. Indices can change and must be checked again
before selection. Corrupt/stale-owner/overflow cases passed Windows and Quest
hardware unit checks. The full-game diagnostic read all 20 actual server rows
on each platform, then completed its existing private create/join/readiness/
leave/delete cleanup. Evidence: `build/xyz-lan-qa/lobby-browser-validation.json`.
`xyz_xbox_lobby_search` now uses original search `0x19b560`, including display
list replacement and ping-record cleanup. Its owned `0x196680` callback captures
the reply while retaining retail request completion. Reads/joins wait until all
records arrive; timeout or malformed counts close the owned session. The live
PCVR and Quest checks passed four searches: offsets 0 and 20 (20 rows each), an
empty page, then offset 0 with five rows. Existing private join/retry/readiness/
leave/delete and profile cleanup passed afterward. Evidence:
`build/xyz-lan-qa/lobby-pagination-validation.json`.
`xyz_xbox_lobby_join_game` rechecks identity and capacity before a selected row
joins, rejects started or unsupported games, and verifies the joined identity
before IN_GAME. Its successful, stale, full, started, unsupported and changed
response cases pass unit checks; live selected-public-game joining is untested.
The player-facing browser and complete race lifecycle remain open.

`xyz_xbox_race` owns the shared lobby-to-NetGame handoff. It validates a real
PLAY_READY roster, the original peer count/host role, empty NetGame, stable
frontend and connected selected logical controller before taking ownership.
It uses the original roster/configuration/profile/initialization operations,
releases the lobby before world allocation, and resolves the exact virtual
host for guests. The diagnostic now calls this interface instead of duplicating
its setup. Return queues the original main-loop transition without replacing
another pending transition. Close waits for a stable frontend with no world
or local player, calls original stop/reset, verifies all players were disposed,
and then restores the prior empty manager. Profile state is restored only if
it still matches the state this handoff wrote. Unit checks cover partial
preparation/cleanup failures and retry, foreign mode, duplicate/missing players,
disconnected input and deferred world teardown. This interface supplies no
fabricated race results or car movement and is not yet wired to a player menu.
The current PCVR/Quest pair passed the shared handoff, countdown, bidirectional
driving and deferred return/close. Displacement was 254.4 m / 237.8 m; median
remote position errors were 0.475 m / 0.408 m. The session stayed at generation
1, the final world and NetGame were empty, and controller/career/Quest options
were restored. Evidence: `build/xyz-lan-qa/race-handoff-motion-validation.json`
and `race-handoff-motion-comparison.json`. The preceding run already passed
movement but the diagnostic incorrectly treated the first unsettled frontend
frame as failed cleanup; that run remains in the `*-frontend-settling` artifacts.
The fixed diagnostic waits for the shared close gate, without bypassing it.

Both native game logs and the user confirmed CONNECTED in free roam on PCVR
and standalone Quest. Those initial sessions had no peers. The subsequent
standalone Quest test displayed a moving QA peer through the actual service:
the user confirmed it and the native log reports one visible car, no failures,
no fallback type and no missing parts. The equivalent PCVR test was explicitly
skipped. The Quest app was subsequently closed and the peer departed; an
explicit offline UI event was not captured. Browser-player interoperability
still needs acceptance.

The game-thread hook is before Xbox `0x1faed0`, only from main-loop return
`0x18ab7f`: after WorldDoTimestep, before camera/rendering. The local reader is
currently opt-in for development with `NFSU2_XYZ_VEHICLE_TRACE=1`.
`NFSU2_XYZ_CAPTURE=<file>` additionally saves the first valid local wire car
record (free roam or a race, with the correct status), without changing the
career. Sampling alone never creates a connection; the lobby diagnostic above
requires its separate explicit flags. The isolated Xbox race capture
on 2026-11-08 is PEUGOT, 89 parts, performance present, 882 bytes. The optional
`xyz_xbox_vehicle_test <capture.cfg>` check decodes/re-encodes it identically
on both Windows and Quest hardware. The tested original career was unchanged.
`NFSU2_XYZ_RIDE_PROBE=1` rebuilds the captured car in owned memory and compares
its wire record, with an optional `NFSU2_XYZ_RIDE_PROBE_OUT=<file>`. The actual
PEUGOT rebuild is identical, with no fallback or missing parts.
`NFSU2_XYZ_CLONE_PROBE=1` creates an additional owned car, loads the local car's
received equipment into it and moves it in a six-metre circle, hiding briefly
at four seconds, before retiring it after ten seconds. Set
`NFSU2_XYZ_CLONE_PROBE_SECONDS` to 10..300 for a longer headset check. This opt-in
diagnostic works on a private test game; it does not connect to a server.
Creation, replacement by the exact 882-byte captured car and removal have been
exercised in an isolated Xbox race. The resulting actual car re-encodes to the
same wire record. A 271-frame movement run has zero failures and zero COM error;
quaternion-dot error is 0.00000012. PCVR headset appearance passed the user's
check with a second stock type; the subsequent Quest standalone visual check
also passed, with a fresh log confirming the 882-byte equipment record.
Streamed-area/world transitions and aftermarket pack ownership still need
their acceptance checks; live-service QA movement now passes on Quest.
`NFSU2_XYZ_ASSET_PROBE=1` exercises the shared/rim header manager in an owned
race without opening a network connection. It preserves the retail Xbox's
mixed-case AUDIO filename. The manager keeps extra header references for the
process lifetime, but does not pin model data. Before car creation/equipment
changes and around frontend Wake, bounded repair merges duplicate loaded
models while preserving drawing users and reference counts. Its fixture checks
run on Windows and Quest. Both an isolated Xbox race and the Quest live-peer
run loaded all 35 headers and reached ready. Garage duplicate-model handling
still needs runtime verification.
