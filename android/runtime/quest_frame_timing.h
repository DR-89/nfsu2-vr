#pragma once
#include <stdint.h>
#include <string.h>

/* Explicit headset rate selection; the old option remains a 72 Hz alias.
 * Runtime support is checked by OpenXR before requesting a rate. */
static inline unsigned quest_refresh_option(const char *rate, const char *legacy)
{
    if(rate && *rate) {
        if(!strcmp(rate,"72")) return 72;
        if(!strcmp(rate,"80")) return 80;
        if(!strcmp(rate,"90")) return 90;
        if(!strcmp(rate,"120")) return 120;
        return 0;
    }
    return legacy && *legacy=='1' ? 72:0;
}

/* NTSC-U D3D's sub_002F2080 decodes command kind from bits 0..4,
 * vblank interval from 5..7, and the immediate-if-late flag from bit 8.
 * Framebuffer/other payload bits must survive unchanged. */
static inline uint32_t quest_present_60hz(uint32_t command)
{
    if ((command & 31) == 1 && ((command >> 5) & 7) > 1)
        return (command & ~(7u << 5)) | (1u << 5);
    return command;
}

/* The race sometimes asks for a 1/30 s minimum simulation step. Keeping it
 * while presenting at 60 Hz would advance the simulation twice too quickly. */
static inline float quest_sim_minimum_60hz(float original)
{
    return original > 1.0f/60.0f ? 1.0f/60.0f : original;
}

/* Above the retail vblank rate, OpenXR paces presentation. Interval zero
 * alone still waits for the next vblank in sub_002F2080; its immediate flag
 * must also be set. Preserve command kind and framebuffer payload. */
static inline uint32_t quest_present_xr(uint32_t command)
{
    return (command & 31) == 1 ? (command & ~(7u << 5)) | (1u << 8) : command;
}

/* OpenXR intervals can jitter around 1/72 s. Flooring each short interval to
 * 1/72 while keeping longer intervals advances time too fast. Let the retail
 * accumulator/quantizer consume elapsed time, retaining its maximum step. */
static inline float quest_sim_minimum_xr(float original)
{
    return original > 0.0f ? 0.0f : original;
}
