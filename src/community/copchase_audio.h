#pragma once
#include "copchase_state.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* CopChase by Vitorini Mods, v0.3.2 native audio adapter. Optional original
 * PCM files live at <parent>/COPCHASE/resources/audio. Startup/shutdown must
 * not run inside an audio callback. Loading does not enable playback. */
int copchase_audio_start(const char *parent);
void copchase_audio_stop(void);
int copchase_audio_available(void);
/* True only after a playable heat-1 or fallback music WAV was validated.
 * A later initial-track open failure withdraws this claim until restart, so
 * the host can release ownership of its retail/custom music pause. */
int copchase_audio_music_available(void);

typedef struct {
    uint64_t generation;
    int enabled, running, paused, music;
    enum CopChasePhase phase;
    int heat;
    uint32_t events; /* A single consumed CopChaseOutput.events snapshot. */
    float master_volume, music_volume; /* 0..1; original music default .25. */
    /* Verified tyre adapter state. new_flat_tyres is an event: publish once,
     * then zero, just like events above. Multiple punctures coalesce into the
     * original single cc_pop voice. RimVolume in the original INI defaults to
     * 60 percent; zero/disabled suppresses only the scrape loop. */
    int flat_tyres, new_flat_tyres, rim_sound, rim_volume_percent;
    float speed_mps;
    /* Verified MW vocabulary only; empty means unknown. Unknown identities
     * select generic chatter, never announce an invented make or colour. */
    char make[16], vehicle_class[16], colour[8];
} CopChaseAudioStatus;

/* Game thread. Copies the snapshot and events; no guest pointers retained.
 * The caller fans the same state snapshot out to HUD/audio, rather than
 * having audio consume the game adapter's pending event bits itself. */
void copchase_audio_publish(const CopChaseAudioStatus *status);
/* Additional verified gameplay incident. Priority 0/1/2 follows the original
 * dispatch policy. Returns whether queued, not a promise of audible playback.
 * Unknown/missing banks (including the absent scanner clips) return zero. */
int copchase_audio_event(const char *bank, int priority, const char *fallback);
/* Audio thread: add to interleaved stereo floats at 48000 Hz; bounded, no
 * allocation/file IO/blocking lock. Returns frames mixed, or zero unchanged.
 * A stale game heartbeat (>500 ms), pause or disabled setting silences it. */
size_t copchase_audio_mix(float *stereo, size_t frames);

typedef struct {
    unsigned banks, clips, queued, buffered_frames, open_streams;
    uint64_t events_played, events_dropped, missing_clips;
    uint64_t tyre_pops_played;
    unsigned rim_volume_milli; /* Actual loop gain on original MCI 0..1000 scale. */
} CopChaseAudioStats;
/* Non-audio thread diagnostic snapshot. */
CopChaseAudioStats copchase_audio_stats(void);
#ifdef __cplusplus
}
#endif
