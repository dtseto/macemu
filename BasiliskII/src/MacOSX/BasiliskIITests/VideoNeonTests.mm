#import <XCTest/XCTest.h>

#include "../../SDL/video_neon.h"

#include <algorithm>
#include <vector>

@interface VideoNeonTests : XCTestCase
@end

@implementation VideoNeonTests

- (void)testNeonComparisonMatchesMemcmp
{
    const size_t lengths[] = {0, 1, 15, 16, 17, 63, 64, 65, 1024, 65536};
    const size_t lengthCount = sizeof(lengths) / sizeof(lengths[0]);
    const size_t maxLength = lengths[lengthCount - 1];

    std::vector<uint8_t> first(maxLength, 0x5a);
    std::vector<uint8_t> second(maxLength, 0x5a);

    for (size_t index = 0; index < lengthCount; ++index) {
        const size_t length = lengths[index];
        XCTAssertEqual(neon_memcmp_differs(first.data(), second.data(), length), false);
        XCTAssertEqual(neon_memcmp_differs(first.data(), first.data(), length), false);

        if (length == 0)
            continue;

        std::vector<size_t> differences = {0, length - 1};
        for (size_t boundary = 16; boundary < length; boundary += 16)
            differences.push_back(boundary);

        for (size_t offset : differences) {
            second[offset] ^= 0xff;
            XCTAssertEqual(neon_memcmp_differs(first.data(), second.data(), length),
                           memcmp(first.data(), second.data(), length) != 0);
            second[offset] ^= 0xff;
        }
    }
}

@end
