# NFSU2 Switch performance work — handoff (2026-09-29)

Goal: race / open-world fps on the Switch (was 6–12 fps in races, menus fine).
This file is the state at the end of the session: what was measured, what was
fixed, what is committed nowhere yet, and what to do next. `CLAUDE.md` has the
same findings in short form.

## Where things stand

| Console race (handheld) | fps |
|---|---|
| start of session | 6–12 |
| + renderer caches, direct vertices, frame lag, GL thread, GIL tweaks | 11–20 |
| + register locals, x87 top local, native culling, clock calls keep GIL | **19.5–22.7** (console run 22:36) |
| + DXT upload, constant rows, ring flags, shader cache | not yet run on the console |

Latest console build: `switch_sd/switch/nfsu2x/nfsu2x.nro` (built 19:42 UTC,
md5 `11e351b9dc6e…`), with `progcache.bin` (67 shaders) and `nfsu2x_env.txt`
next to it. `nfsu2x_old.nro` = the 13:59 build for A/B. Console logs are
archived per run in `switch_sd/switch/logs/run_HHMM/`.

At 19.5–22.7 fps the **GL thread is the limit** (87–93% of a core, idle 1–6%,
executor held back by the full queue 14–36%); the game's main thread is at
59–67%. The build after that run targets the GL thread (DXT, constants, map
flags) and shader stalls.

## Patches applied (2026-09-29 20:29 UTC)

Both patches below are now applied to `/root/nfsu2x/xboxrecomp-pr128`, the
vendored `xboxrecomp/` and `src/`; `/root/nfsu2x/gen` regenerated (LIFT_ONLY)
and the Switch NRO rebuilt (md5 `4779574b…`, ELF kept as
`/root/nfsu2x/elf_archive/nfsu2x_4779574b.elf` for prof_report). Pre-apply
backup: `/root/nfsu2x/perf-wip/pre-apply-2029/`. Still uncommitted.
Console run of the 19:42 build (23:13 console time): race 19-21 fps, hitches
509 -> 325, worst 2184 -> 414 ms, 2 compiles in play (was 54).

### Original note (before applying)

The later work lives in **patches**, saved in `/root/nfsu2x/perf-wip/`:

| Path | What |
|---|---|
| `toolkit.patch` | against `/root/nfsu2x/xboxrecomp-pr128` (and the vendored `xboxrecomp/`): translator passes, recomp_types.h, kernel_bridge.c (GIL priority, clock calls), nv2a_gl.c (hitch log, shader cache, DXT, constants, ring flags), gl_api.h |
| `repo.patch` | against this repo's `src/recomp_manual.c` (native culling sub_0009A330 / sub_0009A250) |
| `xboxrecomp-perf/` | full toolkit copy with everything applied |
| `repo-src/` | full `src/` copy with everything applied |
| `bench.sh`, `cat.py`, `findpid.sh` | Linux benchmark and profile helpers (paths inside point at the old scratch dir; adjust) |

Both patches applied cleanly (dry run) at the end of the session. To bring
them in:

```sh
cd /root/nfsu2x/xboxrecomp-pr128 && patch -p1 < /root/nfsu2x/perf-wip/toolkit.patch
cd /mnt/c/Users/antox/Documents/nfsu2-xbox/xboxrecomp && patch -p1 < /root/nfsu2x/perf-wip/toolkit.patch   # vendored copy
cd /mnt/c/Users/antox/Documents/nfsu2-xbox && patch -p1 < /root/nfsu2x/perf-wip/repo.patch
tools/regen.sh    # LIFT_ONLY=1 is enough; the translator passes and the manual overrides need a regen
```

Regen rewrites the shared `/root/nfsu2x/gen/` (another session builds from it;
`RECOMP_REG_LOCALS=0 RECOMP_X87_LOCALS=0` at regen gives the old output). All
other changes are already in both toolkit trees and in `src/` (see the list
below). Nothing is committed.

## Fixes, in the order they were found

### Renderer / executor (in the trees)
- **Texture and program lookups** (`tex_get`, `prog_get`): hash chains
  instead of scanning 1024 slots per stage per draw; last-program fast path;
  texture binds and enabled attribute arrays cached.
- **Vertex program hash / constants** skipped when unchanged:
  `Nv2aRawBatch.vp_prog_gen` / `vp_const_gen` from the executor.
- **16-bit indices** when a batch has ≤ 65,536 vertices.
- **Direct vertices** (`NV2A_BACKEND_RAW_DIRECT`, `attr_direct`,
  `RECOMP_GL_DIRECT=0` off): the game's vertex bytes uploaded as stored,
  instead of expanding every vertex to 16×float4; only CMP normals converted.
- **Frame lag** (`RECOMP_FRAME_LAG=1`, src/recomp_manual.c): the main loop
  (`sub_000AEA90`, BlockOnFence return 0x000AEDDB) waited for the frame it had
  just built before Present, so the game and the executor took turns. Waiting
  for the previous frame's fence instead: Eden race 9–18 → 21–24 fps.
- **GL thread** (`RECOMP_GL_THREAD=1`, nv2a_gl.c "Threaded submission"): the
  executor queues draws/clears/flips (register shadow, program and constants
  as diffs, vertex ranges and indices copied), a GL thread replays them, at
  most 2 flips queued. Needed: `Nv2aRawBatch.tex_va/pal_va` (pre-resolved
  addresses) and `sample_texture` taking its `Texture` (the decoder had
  borrowed `s_gpu.tex` → magenta textures when racing). Register diffs use
  executor dirty blocks (`nv2a_pb_reg_dirty`; the 8 KB compare was 14% of
  the executor).
- **KickOff flush** (`recomp_spin_wake`, xbox_memory_layout.c): D3D's KickOff
  (0x2E8D40) spins on the PFB write-combine bit (+0x100410 bit 16); the
  polling thread now clears it itself instead of waiting for the flag thread.

### Scheduling (in the trees)
- **GIL handover by guest priority** (`RECOMP_GIL_EAGER=1`,
  kernel_bridge.c): a time-critical waiter (the EA mixer, base priority +16)
  pre-empts the holder at its next function entry; the main thread
  (`xbox_gil_mark_main` in main.c) pre-empts equal/lower holders; everyone else
  keeps the 1 ms rule. Letting *all* higher-priority threads pre-empt cost the
  main thread 31% (stream workers +1/+2); letting only the main thread pre-empt
  made the audio uneven.
- **APU thread priority** (`xbox_nx_raise_host_thread`, 0x2C on Horizon,
  `RECOMP_NX_AUDIO_PRIO=0` off): `SetThreadPriority` is only tracked in
  win32_compat, so the APU's HIGHEST request never reached Horizon.

### Game code (patches)
- **Registers in C locals** (translator `_localize_registers`,
  `RECOMP_REG_LOCALS=0` at regen): eax..edi and esp are shadowed by locals in
  19,394 functions, stored right before every call / ICALL / ITAIL / UNIMPL /
  SPIN_HINT (`_wrap_calls`: after the argument pushes on the same line) and
  reloaded right after the call's statement, stored at every return. Needed:
  `RECOMP_ABI_CALL`'s check reads the real registers, the ICALL failure paths
  store esp/eax through (`RECOMP_ICALL_FAIL_SYNC`), an esp accessor. Registers
  as plain globals is NOT possible: kernel_thunk_dispatch releases the GIL
  before bridges read g_esp / write g_eax. Eden main menu 29–32 → 38–40 fps;
  NRO 4.6% smaller; x86 slightly slower (TLS is cheap there).
- **x87 top in a local** (`_localize_x87_stack`, `RECOMP_X87_LOCALS=0`):
  g_fp_top as a local int, g_fp_stack as a pointer taken once; index stored
  around calls. (Copying all 8 slots per call was slower than the globals.)
- **Native frustum culling** (repo.patch): `sub_0009A330` (box vs 6 planes,
  0 out / 1 straddle / 2 in) and `sub_0009A250` (box × matrix, Arvo) in C with
  the lifted arithmetic (doubles; float rounding where the original stores;
  NaN takes the `jp` branch). `RECOMP_NATIVE=0` off, `RECOMP_NATIVE_CHECK=1`
  runs both and compares: 0 mismatches in 8.4M + 3.1M calls (Linux race).
- **Clock calls keep the GIL** (`kernel_call_keeps_gil`, ordinals 125–128,
  `RECOMP_KERNEL_FAST=0` off): the EA mixer polls DirectSound positions, which
  call KeQuerySystemTime ~400k/s; each call was a GIL handover the
  time-critical mixer won → main thread starved (race frames 0.5–1.5 s,
  9–14 fps stretches). Worst mid-race frame 1540 → 77 ms (Eden).

### GL thread (patches)
- **Shader cache** (`progcache.bin`, `RECOMP_PROG_CACHE=<path>|0`): every
  program compile appends its inputs; `ready()` precompiles the file at boot.
  Console compiles cost 90–150 ms each (52 at race load = 4.5 s). Eden:
  race-start compiles 47 → 3; boot +2.7 s.
- **DXT uploaded compressed** (`RECOMP_GL_DXT=0` off): nearly all race
  textures are DXT1/DXT3 (0x0C/0x0E); `glCompressedTexImage2D` instead of
  a per-texel CPU decode (~70 ms per MB on the console). Swizzled A8R8G8B8
  (0x06/0x07) unswizzled in a loop.
- **Only changed constant rows** sent (runs of `glUniform4fv(u_c + r)`,
  when `c[191]`'s location is `u_c + 191`).
- **Ring maps without `GL_MAP_INVALIDATE_RANGE_BIT`** (nouveau made a staging
  buffer object per map).
- Eden after these: race hitches after 90 s 133 → 33, worst 153 → 61 ms,
  texture uploads 6.6 MB / 151 ms → 0.2 MB / 9 ms.

## Diagnostics added (keep using them)
- `[perf]` lines every 10 s (Switch): fps, APU frames/s (1500 = real time),
  `GL thread idle / executor held back`, `main thread waits by caller`,
  `% of a core per thread` (entries are absolute addresses: subtract the
  `NRO base` from the log and look them up with `aarch64-none-elf-nm`; `__start__`
  is an unrelocated 0, `_start` is the base).
- `[hitch]` lines: every frame > 50 ms with programs compiled / textures
  uploaded and their time.
- **Profiler** `RECOMP_NX_PROFILE=1` → `sdmc:/switch/nfsu2x/prof.bin`
  (1 kHz samples of busy threads: pc, lr, 5 return addresses, 10 ms
  timestamp). `python3 tools/prof_report.py prof.bin <elf> nfsu2x_log.txt
  [--top N] [--time A-B]`. Use the ELF of the exact build. In Eden (no
  thread tick counts) it samples only the main thread.
- Linux: `RECOMP_FPS_LOG=1` prints fps every 10 s.

## Measuring: what worked and what misled
- **Console at stock clocks is the only real measure.** Eden race fps sits at
  ~22 for every build (Eden itself caps it); Eden *menus* and Eden `[hitch]`
  lines were useful. x86 Linux hides everything TLS-related.
- Linux races are not repeatable (same build 15.0 vs 10.9 ms/frame); the
  main-menu idle is (`bench.sh`-style: `/proc/<pid>/task/<pid>/stat` utime per
  frame, `RECOMP_FPS_LOG=1`).
- Check `nfsu2x_env.txt` before trusting a console run: one run had every
  setting commented out and measured the old configuration.
- Eden runs overwrite `switch_sd/switch/nfsu2x/nfsu2x_log.txt` (same folder
  as the console's). Do not restore an old log after a test: it hid a user
  run once.
- Pad scripts are time-based: faster builds reach menus earlier and the
  presses land elsewhere (one run sat at the "Transmission" prompt). Add spare
  `a` presses.

## Eden script
`/root/nfsu2x/run_eden.sh <secs>` (`NRO=name.nro` for another NRO): closes
Eden politely, force-kills only after 15 s, refuses to start a second Eden,
restores eden.exe / qt-config.ini from `/root/nfsu2x/eden_backup/` if a hard
kill damaged them (eden.exe had stopped starting after repeated `taskkill /F`).

## Not done / next steps
1. **Console run of the 19:42 build with progcache.bin on the card** — check
   `[hitch]` at race start and whether the GL thread is still at ~90%.
2. **Port the patches** (above), regen the shared gen/, rebuild, commit when the
   user asks.
3. **LTO** for the game code: `-DNFSU2_GEN_OPT="-O2;-flto"` and
   `-DCMAKE_EXE_LINKER_FLAGS_RELEASE="-flto=4 -flto-partition=balanced"`.
   Do not set `CMAKE_EXE_LINKER_FLAGS` itself: it replaces the Switch
   toolchain's flags (-L for libnx) and CMake's compiler test fails. The retry
   was stopped before it finished; memory/time unknown.
4. **PGO** (see StevensND/nfsmw-nx docs/toolchain.md for the Horizon pitfalls:
   `-fno-profile-values`, `-fprofile-correction`).
5. **Race start** still has the loading → race frame (~0.8 s in Eden: the
   game's one-time setup in `sub_001F9E40` and callers, plus 6–14 MB of
   first-time textures) and a few 100–300 ms frames at the countdown end
   (game logic, `sub_001EF9A0` → `sub_001B…`). Only faster game code helps.
6. **GL thread**: remaining cost is spread over Mesa 20.1's draw path
   (`nvc0_*` validation, kickoffs). Merging draws with identical state, or a
   Vulkan backend on mesa-switch/NVK (StevensND/nfsmw-nx: 32–35 fps with
   that route) are the bigger levers once the GL thread is the limit again.
7. More native rewrites of hot game functions (next in the profiles:
   `sub_000A3CA0`, `sub_002A68EC`, `sub_000AD7D0`); validate each with a
   check mode like `RECOMP_NATIVE_CHECK`.
8. The main thread's remaining waits on handle 0x48000008 (XAPI
   WaitForSingleObject 0x21B475, EA audio lock) are ~1–5%; the new
   `[perf] main thread waits by caller` line tracks them.

## References
- StevensND/nfsmw-nx — NFS Most Wanted 360 Switch port at 32–35 fps: native
  Vulkan renderer on NVK (mesa-switch), shader library, LTO/PGO, native
  rewrites; docs/performance-history.md, measuring.md, platform-notes.md
  (only priority 0x3B time-slices; A57 atomics are slow; apm 0x92220008 =
  GPU 460.8 MHz handheld).
- DemoxDev/NFSMW-Recompiled (switch-vulkan) — NXVK, another NVK port.
