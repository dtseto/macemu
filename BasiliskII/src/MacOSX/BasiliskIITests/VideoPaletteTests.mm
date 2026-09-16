#import <XCTest/XCTest.h>

#include "../../SDL/video_palette.h"

@interface VideoPaletteTests : XCTestCase
@end

@implementation VideoPaletteTests

- (void)testDirtyRectangleExpansionAndPaletteChanges
{
    const int sourcePitch = 8;
    const int destinationPitch = 24;
    const int height = 3;
    uint8_t source[sourcePitch * height] = {};
    uint8_t destination[destinationPitch * height];
    uint32_t palette[256];

    memset(destination, 0xa5, sizeof(destination));
    for (int index = 0; index < 256; ++index)
        palette[index] = 0xff000000u | (uint32_t)index * 0x00010101u;

    source[sourcePitch + 1] = 3;
    source[sourcePitch + 2] = 7;
    source[sourcePitch + 3] = 11;
    video_expand_indexed_rect(source, sourcePitch, destination, destinationPitch,
                              1, 1, 3, 1, palette);

    const uint32_t *row = (const uint32_t *)(destination + destinationPitch);
    XCTAssertEqual(row[1], palette[3]);
    XCTAssertEqual(row[2], palette[7]);
    XCTAssertEqual(row[3], palette[11]);

    const uint32_t untouched = 0xa5a5a5a5u;
    XCTAssertEqual(row[0], untouched);
    XCTAssertEqual(row[4], untouched);
    XCTAssertEqual(*(const uint32_t *)destination, untouched);
    XCTAssertEqual(*(const uint32_t *)(destination + destinationPitch * 2), untouched);

    palette[7] = 0x12345678u;
    video_expand_indexed_rect(source, sourcePitch, destination, destinationPitch,
                              2, 1, 1, 1, palette);
    XCTAssertEqual(row[2], 0x12345678u);
}

@end
