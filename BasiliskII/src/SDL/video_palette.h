#ifndef VIDEO_PALETTE_H
#define VIDEO_PALETTE_H

#include <stddef.h>
#include <stdint.h>

static inline void video_expand_indexed_rect(
    const uint8_t *source, size_t source_pitch,
    uint8_t *destination, size_t destination_pitch,
    int x, int y, int width, int height,
    const uint32_t packed_palette[256])
{
    for (int row = 0; row < height; ++row) {
        const uint8_t *source_row = source + (y + row) * source_pitch + x;
        uint32_t *destination_row = (uint32_t *)(destination + (y + row) * destination_pitch) + x;
        for (int column = 0; column < width; ++column)
            destination_row[column] = packed_palette[source_row[column]];
    }
}

#endif
