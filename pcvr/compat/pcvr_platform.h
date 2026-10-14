#pragma once
#ifdef _WIN32
#include <windows.h>
#include <time.h>
#include <stdint.h>
static inline uint32_t pcvr_counter_milliseconds(void)
{
    LARGE_INTEGER ticks, frequency;
    QueryPerformanceCounter(&ticks); QueryPerformanceFrequency(&frequency);
    return (uint32_t)((ticks.QuadPart/frequency.QuadPart)*1000+
                     (ticks.QuadPart%frequency.QuadPart)*1000/frequency.QuadPart);
}
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
static inline int clock_gettime(int clock_id, struct timespec *ts)
{
    LARGE_INTEGER ticks, frequency;
    (void)clock_id;
    QueryPerformanceCounter(&ticks); QueryPerformanceFrequency(&frequency);
    ts->tv_sec=(time_t)(ticks.QuadPart/frequency.QuadPart);
    ts->tv_nsec=(long)((ticks.QuadPart%frequency.QuadPart)*1000000000LL/frequency.QuadPart);
    return 0;
}
#endif
#endif
