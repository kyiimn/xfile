#!/bin/sh
# Test runner for libx11dnd — starts Xvfb, runs all tests
set -e

XVFB=$(which Xvfb 2>/dev/null || echo "")
if [ -z "$XVFB" ]; then
    echo "Xvfb not found — running tests without X server"
fi

DISPLAY_NUM=99
XVFB_PID=""
if [ -n "$XVFB" ]; then
    $XVFB :$DISPLAY_NUM -screen 0 800x600x24 &
    XVFB_PID=$!
    export DISPLAY=:$DISPLAY_NUM
    sleep 1
fi

cleanup() {
    if [ -n "$XVFB_PID" ]; then
        kill $XVFB_PID 2>/dev/null || true
    fi
}
trap cleanup EXIT

FAILURES=0
for test_bin in test_atoms test_target_aware test_util test_reply test_source test_source_selection test_action test_proxy test_dsave test_incr; do
    if [ -x "./$test_bin" ]; then
        echo "=== Running $test_bin ==="
        ./$test_bin
        if [ $? -ne 0 ]; then
            echo "FAIL: $test_bin"
            FAILURES=$((FAILURES + 1))
        else
            echo "PASS: $test_bin"
        fi
    fi
done

if [ $FAILURES -eq 0 ]; then
    echo "All tests passed"
    exit 0
else
    echo "$FAILURES test(s) failed"
    exit 1
fi