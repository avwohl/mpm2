#!/bin/bash
set -o errexit
#set -o verbose
#set -o xtrace
#
# MP/M II Emulator Test Runner
#
# Starts the emulator, runs SSH tests, and reports results.
#
# Usage: ./run_tests.sh [test_name]
#
# Tests:
#   basic   - DIR and STAT commands
#   all     - All tests
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
DISKS_DIR="$PROJECT_DIR/disks"

PORT=2222
EMU_PID=""

cleanup() {
    if [ -n "$EMU_PID" ]; then
        echo "Stopping emulator (PID $EMU_PID)..."
        kill $EMU_PID 2>/dev/null || true
        wait $EMU_PID 2>/dev/null || true
    fi
}

trap cleanup EXIT

start_emulator() {
    echo "Starting MP/M II emulator..."

    # Check prerequisites
    if [ ! -f "$BUILD_DIR/mpm2_emu" ]; then
        echo "ERROR: Emulator not built. Run ./scripts/build_all.sh first."
        exit 1
    fi

    if [ ! -f "$DISKS_DIR/mpm.sys" ]; then
        echo "ERROR: mpm.sys not found. Run ./scripts/build_all.sh first."
        exit 1
    fi

    if [ ! -f "$DISKS_DIR/mpm2_system.img" ]; then
        echo "ERROR: Disk image not found. Run ./scripts/build_all.sh first."
        exit 1
    fi

    # Start emulator in background (--no-auth for CI/testing)
    cd "$BUILD_DIR"
    ./mpm2_emu --no-auth -p $PORT \
        -s "$DISKS_DIR/mpm.sys" \
        -d "A:$DISKS_DIR/mpm2_system.img" \
        > /tmp/mpm2_test.log 2>&1 &
    EMU_PID=$!

    echo "Emulator started with PID $EMU_PID"

    # Wait for SSH server to be ready
    echo "Waiting for SSH server on port $PORT..."
    for i in {1..30}; do
        if nc -z localhost $PORT 2>/dev/null; then
            echo "SSH server is ready!"
            # Give it a moment to fully initialize
            sleep 1
            return 0
        fi
        sleep 0.5
    done

    echo "ERROR: SSH server did not start within 15 seconds"
    echo "Emulator log:"
    cat /tmp/mpm2_test.log
    exit 1
}

run_expect_test() {
    local test_name="$1"
    shift
    local commands="$@"

    echo ""
    echo "=== Running test: $test_name ==="
    echo "Commands: $commands"
    echo ""

    if "$SCRIPT_DIR/test_ssh.exp" $PORT $commands; then
        echo ""
        echo ">>> TEST PASSED: $test_name"
        return 0
    else
        echo ""
        echo ">>> TEST FAILED: $test_name"
        return 1
    fi
}

test_basic() {
    echo ""
    echo "========================================"
    echo "Running basic tests"
    echo "========================================"

    run_expect_test "DIR command" "dir"
    run_expect_test "DIR with wildcard" "dir *.*"
}

test_stat() {
    echo ""
    echo "========================================"
    echo "Running STAT tests"
    echo "========================================"

    run_expect_test "STAT command" "stat"
    run_expect_test "STAT drive" "stat a:"
}

test_all() {
    test_basic
    test_stat
}

# Main
TEST="${1:-basic}"

echo "MP/M II Emulator Test Runner"
echo "============================"
echo "Test: $TEST"
echo ""

start_emulator

case "$TEST" in
    basic)
        test_basic
        ;;
    stat)
        test_stat
        ;;
    all)
        test_all
        ;;
    interactive)
        echo "Starting interactive SSH session..."
        "$SCRIPT_DIR/test_ssh.exp" $PORT
        ;;
    *)
        echo "Unknown test: $TEST"
        echo "Available tests: basic, stat, all, interactive"
        exit 1
        ;;
esac

echo ""
echo "All tests completed!"
