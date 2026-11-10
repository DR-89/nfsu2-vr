# nfsu2.xyz integration

Status: 9 November 2026. Targets: Windows PCVR and standalone Quest 3.
The implementation and build deliverable is ready for the user's final test.
At the user's request on 9 November, further headset checks are deferred to
that final test. Builds and isolated runtime checks do not count as headset
or device acceptance; the remaining checks are recorded below.

## Controls

- **VR Settings → Online Profiles**: create, select, rename and delete up to
  eight connection profiles. An empty ROOM selects public Free Roam. A named
  room selects a separate world. **CONNECT FREE ROAM** requires a loaded
  career in Explore. **DISCONNECT** retires the owned remote cars.
- **LINK KEYPHRASE** links an existing reserved name. Enter its eight words in
  the masked controller keyboard; hold Y to reveal. Submitting an empty entry
  removes the local credential. Creating a local profile does not reserve a
  service name. Never enter or request the keyphrase in chat or logs.
- **RESERVE / MANAGE NAME** guides new reservations and changes through the
  official website. **OPEN NFSU2.XYZ** opens the system browser from the root
  main menu with online play disconnected; save a loaded career and return
  there first. On the site select **Online → Your name → Claim / Change name**,
  finish its verification and keep its eight-word keyphrase. Return to the
  port and use **LINK KEYPHRASE** with the same local profile name. The URL
  contains no player data or credential; VR preferences are saved first.
- Every connection and reconnect checks the requested name with the service
  before opening the game connection. Reserved/default/blocked names and a
  keyphrase belonging to another name produce a clear error. The Race Browser
  retains this error when it closes. A failed check never reports CONNECTED.
- **BROWSE RACES** works from the root main menu and a loaded career's pause
  menu. The career handoff preserves position/heading, retires remote cars and
  unloads Explore through the original game. Closing the browser returns to
  Explore. Free Roam is not reconnected automatically.
- The race list shows localized map names, host/game names and player counts.
  Details include mode, direction and laps. **JOIN** accepts a password when
  required; **READY** marks the player ready. The host starts the race.
  **CREATE** exposes supported tracks, direction, laps, player limits,
  collision/catch-up and optional password. Created races are unranked.
  Leave returns to the browser; closing it ends the session.
- **ONLINE CARS** sets 1–64 simultaneous Free Roam cars. Default: six. Saved
  preferences are retained. A higher limit increases memory/loading demands;
  it does not create players or override service peer assignments.
- **COMMUNITY MODS** contains the native controls below. New options default
  off. For options marked restart, save settings and restart the app.

Connection profiles and protected credentials are independent of career saves.
The port continues its Xbox career. PC/Xbox save conversion is not implemented;
there is no direct PC-save import.

## Mods

The live catalogue was checked against public reference commit
16b23865f09dc41b9533635180e6d3fd34c45a6d: 16 active mods. The compatibility
JSON preserves the original pinned audit and records current hashes separately.
A public repository revision is not proof of the exact deployed server code.

| Mod | Native behavior and evidence | Remaining validation |
|---|---|---|
| Upscaled HUD | Vulkan replacements; actual HUD substitution observed. Restart. | Quest memory/frame time and headset readability. |
| Detailed Map | Converted 2048 textures and track calibration. Actual original career world map opens, binds the 2048 replacement and selects GPS destinations. Restart. | Headset placement and Quest performance. |
| English Subtitles Fixed | 107 complete lines through the original Xbox movie clock/layout; actual load and timed cues observed. Restart. | Headset readability throughout movies. |
| Uncensored & Restored Soundtrack | Optional verified EA Trax overlay; source PCM independently decoded; actual playback plus six-car test pass. Restart. | Headset listening and Quest pack installation. |
| Transmission Whine | Three audio layers, speed/gear/reverse and Master/Engine volume; mixing/resampling tests pass. | In-game listening and headset balance. |
| GPS Distance | Straight-line distance to the active GPS destination on the VR navigation panel, in game units. Actual offline career drive over 142 m, two original GPS targets and disable/cancel/enable through the map pass. | Route placement and units display in both headsets. |
| HQ Vinyls & Doorlines | Optional 31-car 1024 packs, native colours/gloss/swatches. Frontend, relocated race textures and six remote cars reach Vulkan. Restart. | Quest/headset performance. |
| Stance | Front/rear camber and offset, tire width/reset, per-model sidecar and Free Roam extension 4. Render-only; original handling. Six-car draw restoration passes. | Headset appearance and live browser-player acceptance. |
| Show Hidden Races | Original Xbox career-table edits in memory; both archive fixtures pass. Restart. | Career progression/headset checks. |
| Visual Reputation Remover | Shared switch and audited native stage values, default off. | Career progression and original rating display. |
| Rubberbanding | Career-only Original / No Boost / Off; actual career Circuit 4001 with three opponents. Off sets pace/boost to 1; No Boost retains the original slowing of opponents ahead. | Final headset gameplay acceptance. |
| Extra Options: headlights | Original / Off / High Beam; road projector and lamps exercised; state shared in Free Roam. | Headset brightness/beam position. |
| Extra Options: special vinyls | Special category after Sponsor, original price/unlock filtering. Actual Peugeot shop: four available promos, seven excluded. | Other models/stages. |
| Extra Options: Quick Race | Original menu and race loaded with 127 laps and five AI opponents, including a Corsa; drag grid limits retained. | Full long-race/headset check. |
| Extra Options: European cars | Verified local PC-to-Xbox conversion of Corsa/106 geometry, textures and vinyls. Original selection/28-car AI list enabled only with the complete verified pack. Six-car model changes and removal passed. Restart. | Headset appearance and Quest pack installation. |
| Crash Damage | Private native dents, scratches, loose/fallen parts, wheel deformation, ground contact, original repair and Free Roam damage/repair transport. Actual native draws and hood debris verified. Restart. | Headset cockpit/mirror checks, actual wall contact and live browser damage rendering. |

Widescreen Fix's use case is covered by the native tracked stereo projection.
Split screen is not added inside one headset. The PC High FPS Audio Fix does
not address the native Xbox APU. The additional untracked drift chase camera
is not applied over head tracking.

Crash Damage limits detailed remote damage to the nearest eight cars on PCVR
(four on Quest), with hysteresis and an allocation budget. If that budget is
exhausted, a car temporarily uses its undamaged geometry; it stays visible.
This budget is separate from the configurable 1–64 visible online cars.

## Optional assets and credits

Asset preparation preserves the original disc. Do not commit/distribute
retail inputs, generated fixtures or external artwork/audio without the
relevant rights; retain each author's notices.

- Texture packs: tools/prepare-community-textures.py, staged in TEXPACKS.
- Subtitles: tools/prepare-community-subtitles.py.
- Whine audio: tools/prepare-community-audio.py.
- Crash scratches: tools/prepare-community-damage.py; four FRS Mods textures
  and their notice, staged under build/mod-assets/TEXPACKS for APK packaging.
- European cars: tools/prepare-community-regional.py converts the locally
  supplied PC game's Corsa/106 assets. It requires Pillow and NumPy; outputs
  stay under build/. tools/install-community-regional.py verifies all six
  files before PCVR staging or installation on an explicitly selected Quest.
  REGIONAL is separate from the original game archives. Geometry conversion
  relocates the animation tail as well as all solid offsets.
- HQ vinyls: tools/prepare-community-vinyls.py and stage-community-vinyls.py;
  HQVINYLS totals about 3.76 GB. Author: AdrienShadow. The separate
  install-community-vinyls.py targets an explicitly selected Quest.
- Soundtrack: tools/prepare-community-soundtrack.py and
  stage-community-soundtrack.py; SONGS totals about 1.63 GB. Author: xGMVx.
  Select **Original** under Radio Soundtrack so custom radio does not replace
  EA Trax. install-community-soundtrack.py targets an explicitly selected
  Quest. All four overlay files are checked before use, with a separate
  runtime cache. Original ZDIR/ZZDATA files remain intact.

Large optional packs are not embedded in the Quest APK. Prepared local assets,
hashes and notices are under build/optional-mod-assets. No new Quest pack
installation or headset test is claimed.

## Runtime evidence

The verified PCVR build is installed at build/pcvr/game/nfsu2-pcvr.exe.
Use build/xyz-session-qa/PCVR-Verbindungstest.cmd to retain the user's current
game/save location. The previous executable, saves and preferences are backed
up under build/xyz-release-backup-20261109. Development runs used a separate
executable, copied saves and private runtime caches; all owned runs are closed.
The later name-preflight update has its own backup under
build/xyz-release-backup-20261109-names. Its deployment retained all 12 files
across both career folders and the user preferences byte-for-byte.
The reservation guide update retains that same file baseline; its previous
executable and symbols are in build/xyz-release-backup-20261109-name-guide.
The final diagnostic/build update preserves that file baseline as well;
its preceding executable and symbols are in build/xyz-release-backup-20261109-final.

Installed PCVR SHA-256:
`709d21281f4c9ea5ecc5fe901bbe726b466d1ad8eff853452f4cc07ebb20bef3`.
Built Quest debug APK SHA-256:
`fb7a73500fb81b851935fa10e2aececabdcc570103bcc88dd0c780c041584f28`.

| Check | Evidence under build/ | Result |
|---|---|---|
| Profiles and persistence | User acceptance | Passed on PCVR and Quest. |
| Name permission preflight | xyz-session-qa/names-check-all-tests.log; names-check-live-handshake.log; names-check-live-lan.log; names-check-reserved-live.log | 50 native tests pass, including delayed/cancelled checks, reservation changes on reconnect and browser error retention. Real service connects and transfers TCP/UDP; the user's reserved name is denied without its keyphrase. No claim or account change. |
| Name-preflight game integration | xyz-session-qa/names-browser-final-runtime.log | Actual PCVR executable reads three live race-list pages (20/5/20 rows, every row with a map name), closes, reopens and closes again; names restored and original lobby disposed. Isolated save copy, no user career changes. |
| Career map/GPS | xyz-lan-qa/career-mods-navigation-proof.json; xyz-session-qa/career-mods-observe-runtime.log | Actual original career and USB input: 142.365 m displacement, GPS destination changed via the map, disable/cancel/enable observed, and 512→2048 world-map texture bound. Hidden-race loader also reveals 33 races. Copied save only; all 12 protected user files remained identical. No headset claim. |
| Career rubberbanding Off | xyz-lan-qa/career-mods-off-proof.json; career-mods-off-samples.json | Original available career race ca207584, Circuit 4001. 1,200 original AI updates, 570 corrections; original pace reached 0.610003. All three opponents retained pace/boost 1 in 16 snapshots over 31.2 seconds and moved 1.23–1.41 km. Player stayed still; no race finish claim for this run. |
| Career No Boost and normal finish | xyz-lan-qa/career-mods-noboost-proof.json; career-mods-noboost-finish.json; career-mods-noboost-return-state.json | Circuit 4001, two laps, three opponents, original AI driving. Opponent pace down to 0.610026 remains intact with boost 1. Player finishes first in 4:44.33; all four results appear. Original save completes in the isolated copy, the career race record becomes completed, and Explore returns. 5,040 logged updates; no positive original boost occurred in this run, so that clamp is covered by the rule test. All 12 protected user files remain identical. |
| Free Roam | User feedback and session logs | Both connect; Quest saw the live-service QA car. Public PCVR improved substantially in the latest user report. |
| 64-car lifecycle | Earlier 240-second native fleet diagnostic | Model changes, independent paint, heap growth and removal passed; maximum-load headset performance remains open. |
| Native create/join/start/return | xyz-lan-qa/race-create-join-return-validation.json | Password, copied map selection, ready/start, two players, profile/TLS restoration passed. |
| Browser host/native guest | xyz-lan-qa/browser-native-race-proof.json | Actual Circuit 4011, two RX8s, return and close passed. |
| Native host/browser result | xyz-lan-qa/browser-native-finish-events.json; xyz-session-qa/browser-finish-host-runtime.log | Password join, two laps of Resort Loop, native original-AI finish in 3:04.73 received by the browser. Browser showed the original results screen after its stationary guest's 30-second finish timeout. Native return/profile/TLS checks passed. |
| Complete native race | xyz-lan-qa/race-finish-validation.json | Private unranked Circuit 4014, two laps, original AI driving, normal finishes in places 1/2, browser return and profile/TLS restoration passed. No teleport/result injection. |
| Career/browser/Explore | xyz-session-qa/career-subtitles-unload-runtime.log | Two original unload/reload round trips, identical position, cleared lobby. |
| Career/browser/full race/Explore | xyz-session-qa/career-nis-host-runtime.log; career-nis-guest-runtime.log; xyz-lan-qa/career-nis-return-state.json | Actual career pause, password join, correct grid through countdown, two original-AI-driven Circuit 4014 laps and normal finishes in places 1/2. Browser close and Explore reload at the identical position (distance squared 0); profile/TLS preserved and lobby cleared. Connection generation stayed 1. |
| HQ race rendering | xyz-lan-qa/hqvinyl-race-relocation-proof.json | Relocated original-car textures plus all six private remote skins bound by Vulkan; 1,919 ready frames, zero failures, complete removal. |
| HQ alias cache | xyz-lan-qa/hqvinyl-alias-cache-proof.json | Six private remote skins, 1,937 ready frames, zero failures and removal passed. |
| Soundtrack | xyz-lan-qa/restored-production-proof.json | Original-disc virtual overlay plus HQ/six-car test: 1,529 ready frames, zero failures, removal passed. |
| Special vinyls | xyz-session-qa/special-three-runtime.log | Actual Customize/Paint/Vinyl category hook passed. |
| Headlights/combined fleet | xyz-session-qa/lights-hq-fleet-runtime.log | Original/Off/High cycles exercised; 1,540 ready frames, zero failures, removal passed. |
| Crash damage | xyz-lan-qa/damage-current-proof.json | Native dents/scratches/loose parts, detached hood, 114 successful ground queries, repair and two-way damage/repair core transport passed. |
| European cars | xyz-lan-qa/regional-current-proof.json | Six cars, Corsa/106 model changes, paint/stance and removal; 1,530 ready frames, zero failures. |
| Quick Race limits | xyz-lan-qa/quickrace-current-proof.json | Original menu and native race: 127 laps, five AI opponents, including a Corsa. Race started, not completed for 127 laps. |
| Native suite | xyz-lan-qa/mods-races-release-ctest.log | 49 tests passed, including cinematic handoff guards, unrelated-departure storms and reliable address reuse; five regional converter tests passed separately. |
| ARM64 and APK | xyz-lan-qa/career-nis-arm64-build.log; career-nis-apk-package.log | Native build and debug APK packaging passed; CRC, native-library equality, notices and scratch assets verified. No new Quest device test. |
| Final build/package checks | xyz-lan-qa/final-community-ctest-20261109.log; final-community-pcvr-20261109.log; final-community-apk-20261109.log; final-community-artifacts-20261109.json | 50 native tests pass. PCVR and ARM64 compile; APK CRC, unique entries, packaged native-library equality and notices pass. No new Quest installation. |

The prepared soundtrack streams 1, 6, 22 and 43 decode to the source PCM in an
independent decoder. Runtime mixer capture identifies the new audio but has
discontinuities between captured callbacks; it does not establish bit-perfect
or gap-free final APU output. Headset listening remains necessary.

Failed diagnostics are retained. The invalid one-lap Circuit fixture was
rejected before creating a lobby. Earlier archive, race teardown and capture
failures are not counted as successful checks. GPU bindings, game state and
authored menu-renderer images supplied the rendering evidence.

## Implementation boundaries

The native transport owns TLS/WebSocket work. Only bounded copied commands,
state and datagrams cross to the guest thread; callbacks never write game
memory. The virtual UDP envelope is big-endian, the inner roam wire format
little-endian. Received pointers and native structs are never trusted directly.

The Xbox adapter owns its extra cars, private paint and streaming/retirement
barriers. Race sessions use original LAN state machines through the native
virtual socket boundary. The catalogue has 100 usable entries: 31 Circuit,
23 Sprint, 10 Drag, 17 Drift, 11 URL and eight Street X. Direction, laps,
player limits, copied row identity and lobby epoch are checked before joining
or creating. Leased profile fields are restored conditionally, preserving
subsequent unrelated changes.

Career transfer uses original Pause Quit, world unloading and Explore loading.
It waits for active and loading intro/outro/story sequences. The early forced
Pause diagnostic was invalid: its old career NIS later repositioned the race
car. A hardware write watch identified the original NIS call chain. The test
now closes the original SMS tutorial and waits for ordinary pause eligibility.
An owned LAN teardown skips only an audited optional Xbox Live friends-client
call when its client is absent. It does not repair TLS after corruption or
change unrelated callers.

The service's global departure notifications are filtered to peers with
reliable traffic in this connection generation. A departure is retained in
order with that peer's packets; repeated notifications without new reliable
traffic are ignored. This prevents unrelated player churn from filling the
256-entry reliable queue while the guest unloads a world. Missing a relevant
reliable frame or departure still resets the link rather than leaving a
silently damaged stream.

Existing-name credentials are separate from xyz-profiles.json. Real Windows
DPAPI persistence passes. Android AES-GCM/Keystore builds, but device
persistence and actual reserved-name acceptance remain unverified. New name
claims have a guided handoff to the official site, followed by native
keyphrase linking and the service name check. Actual browser launch/return
in the headsets and a complete new reservation remain unverified; no name
was claimed by an automated test. The port does not embed or bypass the
service's human verification.

The native worker checks GET /names over validated HTTPS on Windows and
Android, using only a derived credential in the Authorization header. It
rejects redirects, oversized/malformed replies and stale results after a
cancelled or replaced request. Network calls run outside the game thread;
the game handshake deadline starts after the lookup. WinHTTP/OkHttp fixtures,
ARM64 compilation, Android socket unit tests and APK contents pass. This
read-only check neither creates cloud saves nor reserves a name. New claims
still require the official service's keyphrase registration and any requested
human verification.

The reservation guide is shared by PCVR and Quest. Its controller checks
cover held buttons, missing selections, active/connecting sessions, an active
race browser and a failed URL handler. The native UI host rechecks eligibility
before calling SDL_OpenURL with the fixed official HTTPS URL. The rendered
eight-profile list and both guide states fit the panel. Evidence:
build/xyz-session-qa/names-guide-tests.log and names-guide-{list,main,career}.png;
names-guide-pcvr-build.log and names-guide-arm64-build.log. These checks do not
prove that a headset's external browser completed a claim.

The broader site-feature inventory is an assessment. It does not establish
native friends, crews, chat, cloud saves, photo tools, career co-op or duels.

## References and completion

PC reference: SPEED2.EXE v1.2 North America, 4,800,512 bytes, SHA-256
f9dd86c054878ce6276beb07c1fd61874f7a1e4bf1f241b084c65b73e24168a7.
Xbox hashes and audited function/field evidence are in
config/nfsu2-xyz-xbox-map.json. The read-only compatibility checker never
equates matching references with gameplay or save-import acceptance.

Implementation, automatic release checks and local PCVR deployment are
complete for the requested final user test. Public reconnect/world-transition
acceptance, headset checks and the account-flow checks described above remain
unverified acceptance items. Quest device testing and optional pack installation
are deferred at the user's request. No further intermediate headset check is
required before offering this build for the final test, and no skipped test
is counted as passed.
