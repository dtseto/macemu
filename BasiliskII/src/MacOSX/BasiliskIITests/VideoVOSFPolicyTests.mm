#import <XCTest/XCTest.h>

#include "../../CrossPlatform/video_vosf_policy.h"

@interface VideoVOSFPolicyTests : XCTestCase
@end

@implementation VideoVOSFPolicyTests

- (void)testDefaultAndConfiguredThresholds
{
    XCTAssertEqual(video_vosf_effective_threshold(0), VOSF_DEFAULT_THRESHOLD_USEC);
    XCTAssertEqual(video_vosf_effective_threshold(-1), VOSF_DEFAULT_THRESHOLD_USEC);
    XCTAssertEqual(video_vosf_effective_threshold(12000), 12000u);
}

- (void)testProfitabilityBoundaryIsStrict
{
    const uint32_t tries = 3;
    const uint32_t threshold = 1000;

    XCTAssertTrue(video_vosf_duration_is_profitable(2999, tries, 1, threshold));
    XCTAssertFalse(video_vosf_duration_is_profitable(3000, tries, 1, threshold));
    XCTAssertFalse(video_vosf_duration_is_profitable(3001, tries, 1, threshold));
}

- (void)testFrameSkipScalesAvailableBudget
{
    XCTAssertFalse(video_vosf_duration_is_profitable(5000, 1, 1, 1000));
    XCTAssertTrue(video_vosf_duration_is_profitable(5000, 1, 6, 1000));
    XCTAssertTrue(video_vosf_duration_is_profitable(999, 1, 0, 1000));
    XCTAssertFalse(video_vosf_duration_is_profitable(0, 0, 1, 1000));
}

@end
