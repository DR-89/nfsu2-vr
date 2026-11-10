# CopChase private asset preparation and mounting

The asset importer and optional native mount support the user's original
**CopChase by Vitorini Mods v0.3.2** ZIP. These resources feed the port's native
pursuit, audio and settlement adapters; the importer itself only prepares data.
Runtime behavior and remaining live validation are documented in
[copchase-native.md](copchase-native.md), [copchase-audio.md](copchase-audio.md)
and [copchase-bust.md](copchase-bust.md).
No ASI is executed, and neither preparation nor mounting modifies original
disc archives or career saves. Generated mod/game assets remain private under
ignored `build/` output; the source tree contains only code and metadata.

## Preparation

```powershell
.venv/Scripts/python.exe tools/prepare-copchase.py 'C:/Users/<user>/Downloads/CopChase-v0.3.2.zip'
```

The combined archive also requires the already prepared restored soundtrack's
6,840-byte `MusicSFX.mpf` metadata at `build/optional-mod-assets/SONGS/`, or an
explicit `--restored-mpf` path. Its checksum is verified. The large music stream
is not copied into CopChase; the ordinary soundtrack module still validates
and mounts it independently.

The input ZIP must match SHA256
`f7d68dc92dd2cb78d7dcc3e09e1e17b1444156cd8d674c0e3c72997e5d984f8d`.
The importer also validates its exact file layout, plugin version, PC mesh
streams and texture metadata. Output defaults to `build/mod-assets/copchase`;
custom destinations must remain below `build/mod-assets/` and cannot overlap
the source game or input archive.

All 15 model pairs are converted for offline examination. The default archive
contains the package installer's eight mixed-population slots:
`4DR_SEDAN`, `MINIVAN`, `COUPE`, `TAXI02`, `HATCHBACK`, `BUS`, `PICKUP`, `SUV`.
Only their shared skin entries are replaced. The `--density all` option
prepares all 15 archive slots, but the ordinary runtime accepts
**only the pinned mixed8 pack**. Separately pinned `geometry-only8`, `skins-only8`,
`shared-only8` and `slot-textures-only8`
factor probes are available through `NFSU2_COPCHASE_ASSET_VARIANT` for scripted
diagnosis; they use separate pack subdirectories and caches. These historical
v1 fixtures preserve the missing-global-index failure for reproduction and
are not playable packs. Inactive loose models must not be enabled
without rebuilding the shared skin selection.

`manifest.json` records source/output SHA256 hashes, original duplicate ZDIR
rows, both archive-sector bases, conversion checks and original terms/credits.
The 1,063 supplied WAV files are copied unchanged and validated as PCM16.
The upstream audio manifest names 35 absent `scanner_*.wav` clips. Those
missing references are explicitly recorded; the original manifest is retained,
and `resources/audio/native-manifest.json` excludes unavailable clips.

The Xbox preloads shared texture offsets and compressed lengths from
`GLOBAL/GLOBALMEMORYFILE.BIN`, before reading `CARS/TEXTURES.BIN`. Both copies
of that memory file must be redirected too. Archive revision 2 embeds the
rebuilt shared prefix, updates its total-file-size word, and verifies all
other bytes remain unchanged. The plain and restored-music memory files both
reside in archive 13; each ZDIR selects its corresponding payload. The latter
retains the verified restored MPF at offset 31,088. This is the same logical
global file historically named `MusicSFX.sbk` by soundtrack staging, so the
two modifications are composed rather than independently replacing it.

## Staging and enabling

After preparation, the stager verifies every deployed file before copying.
Close the app before changing its optional data:

```powershell
.venv/Scripts/python.exe tools/install-copchase.py --pcvr build/pcvr/game
.venv/Scripts/python.exe tools/install-copchase.py --quest-staging build/mod-assets/copchase-quest
```

`--verify-only` reports missing/changed destination files without writing.
PCVR staging requires a native executable directory and rejects original PC
or Xbox game roots. Quest staging is offline and stays below `build/`; it
does not contact or install to a device. Its `COPCHASE` folder is intended for
`/sdcard/Android/data/org.nfsu2.quest/files/COPCHASE`. Source paths and all
destination components are bounded and reject symlinks/junctions. Copying
uses verified temporary files and atomic replacement; existing cache folders
and unrelated data are retained. No settings are enabled by the stager.

Both hosts call `copchase_assets_start(parent, quest_xr_copchase_at_start())`
after `vr_soundtrack_start`, before guest threads. The default setting remains
off. Enable CopChase under Community Mods, save and restart to request normal
mounting. An unavailable or invalid requested pack displays a nonblocking
`DATA MISSING` menu value and reinstall hint. Enabling only during the current
session instead shows the required save/restart action. Availability is a
locked runtime snapshot and is never persisted as a user setting.

Scripted QA can additionally request mounting with both process settings:

- `NFSU2_COPCHASE_ASSETS_PROBE` must equal `1` exactly.
- `RECOMP_PAD_SCRIPT` must be nonempty.

The optional pack belongs at `<parent>/COPCHASE/`, matching the case exactly
on Android. `parent` is the executable asset directory on PCVR and the app's
external storage directory on Quest. Diagnostic factor variants still require
both QA settings, even when normal CopChase is enabled; normal launches cannot
select a historical factor pack accidentally.

Mounting validates the retail ZDIR SHA256, all ten retail archive sizes,
absence of source archive slots 10–15, and the selected mixed8 directory and
archive SHA256. The ordinary game importer remains responsible for full
retail-disc payload verification. An active restored soundtrack is detected
through its exact ZDIR redirect and the returned directory checksum.

| Active soundtrack | CopChase directory | Archive 13 base sector |
|---|---|---:|
| Original | `ZDIR.BIN` | 1,188,935 |
| Verified restored pack | `ZDIR-SONGS.BIN` | 1,983,204 |

The exact CD paths for `NFSUNDER/ZDIR.BIN` and `NFSUNDER/ZZDATA13.BIN`
are redirected. Accepted CD prefixes are `D:\`, `\??\D:\`, and
`\Device\CdRom0\`; case and slash direction may vary. For CD requests, relative paths,
additional components, save paths, other archive indices and loose car files
remain outside this hook. The composed directory wins over the soundtrack
directory; existing regional-car and soundtrack fallbacks remain available.

The cache is isolated below the verified optional pack's `RuntimeCache/`,
using separate `copchase-0.3.2-mixed-original-v2` and
`copchase-0.3.2-mixed-restored-v2` directories. Historical factor probes retain
their own v1 caches for reproduction. Resolved paths cannot overlap
the source game or escape the pack. Disabling/reinitializing the module before
guest startup restores its prior `NFSU2_RUNTIME_DIR` value if no other
initializer changed that value meanwhile.

The title also opens `\Device\Harddisk0\Partition1\CACHE\LocalCache*.bin`.
That device prefix normally maps under the game directory, bypassing
`NFSU2_RUNTIME_DIR`. While the verified optional mount is active, this cache tree alone
is redirected to `Partition1Cache/` within the selected isolated namespace.
Case and slash aliases share uppercase cache filenames on both platforms.
Drive links resolved by the kernel re-enter the same path hook. Partition1
`UDATA`, `TDATA`, unrelated directories and other partitions retain ordinary
routing. Disabled or invalid packs claim no cache paths.

Cache path components cannot contain traversal, alternate streams or other
special characters. Linked cache directories/files are rejected, including
links changed after mounting; linked cache roots prevent mounting. Rejected
claimed paths return an empty hook result, which both kernel backends treat
as failure, so an unsafe cache request cannot fall back to the source game.
Cache path results use per-thread storage and are copied immediately by the
kernel path layer. The optional overlay does not relocate career saves.

### Archive gaps

The Xbox archive opener `sub_00183A80` in `gen/recomp_0032.c` checks each slot
independently. At guest address `0x183B30`, an absent file sets that slot's
handle to `0xFFFFFFFF`. Control then reaches `0x183B39`, increments the index,
and continues until index 16. It does not stop at the first absent archive.
Therefore the original-soundtrack variant can leave slots 10–12 absent and
still open slot 13. This is an audit of the existing guest archive opener;
it does not replace an eventual in-game asset rendering test.

## Automatic verification

The focused C++ target is `copchase_assets_test`, registered with CTest and
built by the PCVR bundle and Quest runtime targets. It also supports an
optional private fixture without adding game bytes to the repository:

```powershell
build/copchase-assets-test/copchase_assets_test.exe
build/copchase-assets-test/copchase_assets_test.exe game build/mod-assets/copchase
ctest --test-dir build/copchase-assets-test -R '^(copchase_assets|xyz_regional_assets|vr_soundtrack)$' --output-on-failure
```

The fixture test copies only the prepared overlay into a temporary directory.
It checks both soundtrack variants, exact routing boundaries, community-hook
precedence/fallback, separate caches, disabled defaults, normal opt-in and both probe gates,
missing files, truncation, same-size corruption, and unsupported source or
soundtrack directories. A deliberately isolated soundtrack collaborator stub
uses composed retail metadata; it does not load or substitute for the
soundtrack module's separately tested 1.6 GB audio verification.

The partition1 cache regression checks actual isolated writes, case/slash
aliases, root directory routing, save-path exclusion, traversal and alternate
stream rejection, directory/file symlink escapes and linked-root rejection.
On systems without symlink privileges those particular cases report a skip;
the Windows verification on 2026-11-09 executed them successfully. All three
focused CTest checks and the private fixture passed after the cache fix.

The revision-2 private fixture also extracts both duplicate global-memory
entries directly from the composed archives. It verifies their preloaded
texture prefix and total-file-size field against the streamed shared pack,
preservation of the original MPF in plain mode, and that restored mode differs
only inside the MPF region. Importer tests exercise stale-prefix rejection,
entry bounds, metadata preservation and distinct per-soundtrack payloads.

Verified on 2026-11-09: x64 focused tests and the private fixture passed;
the ARM64/API30 test executable compiled and linked with Android NDK 29.
The linked regional-car private fixture also passed. Importer verification
covered all 75 solids, 279,613 vertices and 748,724 indices, preservation of
538 unrelated shared textures in mixed mode, all 41 affected directory rows
(including both global-memory metadata copies),
and every output file's hash. Both regional car conversion hashes remain
unchanged. These asset checks are distinct from live pursuit validation.
The subsequent private `copchase-fixed` PCVR run also verified that shared
texture decoder inputs match the rebuilt pack after the global-index fix.
The ARM64 executable was not run on a headset during these checks.

The stager's six Python tests cover traversal/reparse rejection, source/output
overlap, original-game rejection, verified atomic replacement, unchanged-file
skips, failed-copy preservation and a read-only comparison. The native menu
test covers all availability states, preservation across pause close, normal
toggle operation and panel bounds. No original assets are checked into source.
