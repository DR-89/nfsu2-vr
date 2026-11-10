# CopChase native audio adapter

`src/community/copchase_audio.{h,cpp}` adapts the audio dispatch behavior of
CopChase v0.3.2 by Vitorini Mods to portable PCM playback. It executes no
Windows plugin code. Original audio stays in the user's private asset folder,
`COPCHASE/resources/audio`, and is not included in source control.

The source ZIP SHA256 is
`f7d68dc92dd2cb78d7dcc3e09e1e17b1444156cd8d674c0e3c72997e5d984f8d`;
the original plugin SHA256 is
`43590d849ba616253e0e975269f7db62537c74013d4f2177fe179ffa0d24ea9e`.
The importer preserves the original audio manifest and chatter annotations.
Its derived manifest omits 35 scanner clips absent from the ZIP and adds the
seven existing `heat10_m*.wav` clips omitted by the original manifest. This
matches the binary's `<bank>_*.wav` directory enumeration. The resulting
37 banks contain all 1,063 supplied WAV files. No replacement voices or
scanner recordings are generated.

## Verified behavior

| Trigger | Original bank or file | Priority |
| --- | --- | --- |
| Pursuit begins | `describe` | 2 |
| Sight lost | `lostvis`, fallback `lost` | 2 |
| Spotted again | `spotted`, fallback `describe` | 2 |
| Escaped | `evaded`, fallback `lost` | 2 |
| Busted | `busted` | 2 |
| Heat changes | `heatN`, heat 10 fallback `heat5` | 2 |
| Pursuit ambience | `status`, `backup`, `roadblock`, `describe`, `status` | 0 |
| Cooldown ambience | `search`, fallback `status` or `backup` | 0 |
| Roadblock/spike creation | `roadblock` / `spikes` | 2 |
| Approaching spike strip | `spikenear` | 2 |
| First wheel impact, voice throttled by gameplay | `spikehit` | 2 |
| Pass roadblock/spikes without impact | `rbmiss` / `spikemiss` | 1 |
| Ram release | `rbcrash` | 2 |

Radio dispatch comes from ASI `0x10024D00`. Priority 2 can interrupt any
line; priorities 0 and 1 only interrupt a lower priority. Chatter annotations
filter explicit make/class/colour descriptions. Unknown identity selects
generic lines only. When both generic and matching lines exist, the original
75% preference for matching descriptions is retained. Missing banks are
skipped or use the caller's explicit fallback. Approximate original WAV
duration is replaced with duration calculated from the actual sample rate.

The main loop `0x100315F0` schedules pursuit ambience every 8–16 seconds
and cooldown searches every 10–18 seconds, with at least 2.5 seconds after
a radio line. The initial describe delay and event mapping follow that loop.
The adapter has a bounded explicit incident API for verified future ram,
crash, roadblock and spike events; it does not infer incidents itself.

Siren helper `0x1002DAC0` loops `siren_3.wav` at gain 0.30 through pursuit
and cooldown. `0x1002DB90` stops it. Music helper `0x1001ECF0` starts
`music_heatN.wav`, using `music_pursuit.wav` if the initial file is missing.
A missing later heat track leaves the current track playing. Default music
gain is 0.25. `0x10026AC0` starts `pursuit_start.wav` at music gain +0.15,
capped at 1. `0x1001EE50` stops music and the sting. Context cancellation,
disable and missing game heartbeat close all voices; pause silences output.

Private decompilation evidence is saved under `build/copchase-re/audio`.
These original paths show no ownership-safe EA TRAX mute/resume mechanism.
Native host integration must use its existing retail/custom radio ownership
code to pause and restore both sources before enabling pursuit music.
`copchase_audio_music_available()` only permits this after a playable heat-1
or fallback WAV has passed RIFF/PCM validation. A subsequent failed initial
track open withdraws availability so the host can release its music pause.
Siren and radio chatter remain available with music disabled. Scanner
proximity cues need original clips, which are absent from the supplied ZIP.

Roadblock helper `0x1002AAA0` (private evidence
`build/copchase-re/xbox/road-helper-108.c`) owns separate `cc_pop` and `cc_rim`
streams. Each explicit new puncture starts `tyre_pop.wav` once at full effect
gain, restarting the preceding pop if necessary. Multiple new flats in one
frame coalesce, as the original boolean trigger does. A flat tyre alone does
not retrigger a pop. `rim_scrape.wav` starts above 4 m/s and remains active
until speed is at most 1.5 m/s, tyres are repaired or rim sound is disabled.
Its MCI gain is `min(500,150+trunc(8*speed))*RimVolume/100`, with integer
division and updates only when the difference is at least 41 units.
The original INI defaults to `RimSound=1`, `RimVolume=60`. The native loop
starts muted until a qualifying gain update, avoiding an unattenuated initial
buffer. Both effects remain independent of the pursuit phase while the host
still reports a valid driving context. No tyre state is inferred from audio.

## Integration contract

Call `copchase_audio_start(parent)` outside the callback. Missing or malformed
optional resources return unavailable and leave normal audio unchanged.
Loading alone does not enable playback. Stop and startup are serialized
host lifecycle operations; call `copchase_audio_stop()` before shutdown.

The game thread publishes an already consumed event snapshot once, then
publishes subsequent frames with zero events. HUD and audio receive the
same snapshot; audio never consumes the game adapter's event queue itself.
`running` means a valid driving context, not an active pursuit: escaped and
busted announcements can finish while `phase` is idle. A different world
generation cancels old voices and pending incidents. Identity strings stay
empty until the native adapter has verified their vocabulary.

The verified tyre adapter supplies `flat_tyres` (0..4), `speed_mps`,
`rim_sound`, and `rim_volume_percent` (0..100, original default 60).
`new_flat_tyres` is a consumed event, published once then cleared just like
`events`. It coalesces on the worker without growing a queue. Pause ignores
new pop events and silences existing effects; context loss or cancellation
closes them. The incident API accepts the roadblock/spike banks above;
gameplay owns distance tests and the original five-second spike-hit voice
throttle. Audio does not fabricate incidents or suppress the tyre pop when
radio chatter is throttled.

The mixer adds interleaved stereo floats at 48 kHz. It does no file I/O or
allocation and uses only a nonblocking mutex attempt. It leaves the caller's
buffer untouched when disabled, paused, empty or when the game heartbeat is
older than 500 ms. PCM16 mono/stereo WAVs are linearly resampled on a worker.
The worker owns six voices, each with a 32 KiB source cache, a 4,096-frame
stereo output ring and a 16-event incident queue. Music is streamed rather
than preloaded. Resource paths, RIFF chunk bounds, PCM format, file size,
bank counts, clip counts and annotations are validated. Optional missing
loops are attempted once per state transition, not repeatedly every block.

## Verification

`tests/copchase_audio_test.cpp` builds synthetic PCM fixtures and checks
24/36 kHz resampling, mono/stereo separation, exact final-frame handling,
radio preemption, generic/matching annotations, fallback, pause/resume,
disable/cancel, stale-heartbeat silence, gain sanitization, missing/corrupt
files, bounded queues, puncture-event coalescing, rim speed hysteresis,
integer gain/threshold behavior, independent overlapping tyre effects and
restart/shutdown. An optional parent-directory
argument validates the private original bank counts and concurrently opens
the actual radio, siren, music, sting, tyre pop and rim scrape. It does not
play through a device.
`tests/test_prepare_copchase.py` checks the additional heat-10 bank and
preservation of the original manifest alongside asset metadata tests.

Verified on 2026-11-09: MSVC x64 `/O2 /W4` synthetic and private-resource
tests passed, including file and manifest symlink rejection and PCM samples
across input-cache boundaries. Android NDK 29 compiled and linked the same
module/test for ARM64/API30 with `-Wall -Wextra -Werror`. Seven importer
tests passed. No audio was sent to a device and no headset listening test
was performed by these checks.

The private staging report `build/copchase-re/audio/staged-audio.json`
records every copied file's byte length, source SHA256, output SHA256 and
Vitorini credit. Staging changes only the optional `COPCHASE` folder, not
retail disc data or saves. Host build wiring and headset listening checks
are separate integration work.
