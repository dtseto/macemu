#ifndef VIDEO_VOSF_POLICY_H
#define VIDEO_VOSF_POLICY_H

#include <stdint.h>

static const uint32_t VOSF_DEFAULT_THRESHOLD_USEC = 16667 / 2;

static inline uint32_t video_vosf_effective_threshold(int32_t configured_threshold)
{
    return configured_threshold > 0 ? (uint32_t)configured_threshold : VOSF_DEFAULT_THRESHOLD_USEC;
}

static inline bool video_vosf_duration_is_profitable(
    uint32_t total_duration,
    uint32_t tries,
    uint32_t frame_skip,
    uint32_t threshold)
{
    if (tries == 0)
        return false;
    const uint32_t refresh_quantum = frame_skip ? frame_skip : 1;
    return (uint64_t)(total_duration / tries) < (uint64_t)threshold * refresh_quantum;
}

#endif
