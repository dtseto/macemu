#!/bin/sh
set -eu

# Asset-free repository smoke test.  Emulator-dependent gates live under
# jit-test/ and are intentionally separate from this deterministic check.
test -f "$(dirname "$0")/../src/MacOSX/BasiliskII.xcodeproj/project.pbxproj"
test -f "$(dirname "$0")/../src/MacOSX/BasiliskIITests/BasiliskIITests.swift"
echo "OK"
