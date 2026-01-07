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
#   basic   - DIR and STAT commands (default)
#   stat    - STAT command variants
#   all     - All basic tests
#   src     - Build from source and run basic tests (optional)
#   interactive - Start interactive SSH session
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

    if [ ! -f "$DISKS_DIR/mpm2_system.img" ]; then
        echo "ERROR: Disk image not found. Run ./scripts/build_all.sh first."
        exit 1
    fi

    # Start emulator in background (--no-auth for CI/testing)
    cd "$BUILD_DIR"
    ./mpm2_emu --no-auth -p $PORT \
        -k "$PROJECT_DIR/keys/ssh_host_rsa_key" \
        -d "A:$DISKS_DIR/mpm2_system.img" \
        > /tmp/mpm2_test.log 2>&1 &
    EMU_PID=$!

    echo "Emulator started with PID $EMU_PID"

    # Wait for SSH server to be ready
    echo "Waiting for SSH server on port $PORT..."
    for i in {1..30}; do
        if nc -z localhost $PORT 2>/dev/null; then
            echo "SSH server is ready!"
            # Wait for MP/M II to fully boot (TMP to print prompt)
            # SSH starts before boot completes, so need extra time
            echo "Waiting for MP/M II to boot..."
            sleep 8
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

    # Delay to let console reset between tests
    sleep 5

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

test_src_build() {
    echo ""
    echo "========================================"
    echo "Running source-built programs test"
    echo "========================================"
    echo ""

    # Stop any running emulator first
    if [ -n "$EMU_PID" ]; then
        echo "Stopping current emulator..."
        kill $EMU_PID 2>/dev/null || true
        wait $EMU_PID 2>/dev/null || true
        EMU_PID=""
    fi

    # Build with source tree
    # Note: Some programs may fail to build due to uplm80 strictness.
    # We continue if the core system files are built successfully.
    echo "Building with --tree=src..."
    "$SCRIPT_DIR/build_all.sh" --tree=src > /tmp/mpm2_src_build.log 2>&1 || {
        echo "WARNING: Some source builds had errors (see /tmp/mpm2_src_build.log)"
        echo "Continuing with partial build..."
    }

    # Check if disk image was created (indicates at least partial success)
    if [ ! -f "$DISKS_DIR/mpm2_system.img" ]; then
        echo "ERROR: Disk image not created. Build completely failed."
        cat /tmp/mpm2_src_build.log
        return 1
    fi

    # Check that key source-built files exist
    # Core system files must be present; utilities are optional
    echo "Verifying source-built binaries..."
    local core_files=(
        "$PROJECT_DIR/bin/src/MPMLDR.COM"
        "$PROJECT_DIR/bin/src/XDOS.SPR"
        "$PROJECT_DIR/bin/src/RESBDOS.SPR"
        "$PROJECT_DIR/bin/src/TMP.SPR"
        "$PROJECT_DIR/bin/src/BNKBDOS.SPR"
    )
    local optional_files=(
        "$PROJECT_DIR/bin/src/DIR.PRL"
        "$PROJECT_DIR/bin/src/STAT.PRL"
    )

    echo "Core system files:"
    local missing_core=0
    for f in "${core_files[@]}"; do
        if [ ! -f "$f" ]; then
            echo "  MISSING: $(basename $f)"
            missing_core=1
        else
            echo "  OK: $(basename $f)"
        fi
    done

    if [ $missing_core -eq 1 ]; then
        echo "ERROR: Core system files are missing. Cannot run test."
        return 1
    fi

    echo "Optional utilities:"
    for f in "${optional_files[@]}"; do
        if [ ! -f "$f" ]; then
            echo "  SKIP: $(basename $f) (build failed)"
        else
            echo "  OK: $(basename $f)"
        fi
    done

    # Start emulator with source-built disk
    echo ""
    echo "Starting emulator with source-built binaries..."
    start_emulator

    # Run basic tests
    test_basic
}

# Main
TEST="${1:-basic}"

echo "MP/M II Emulator Test Runner"
echo "============================"
echo "Test: $TEST"
echo ""

# Start emulator for most tests (src test handles its own startup)
if [ "$TEST" != "src" ]; then
    start_emulator
fi

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
    src)
        # Source-built test builds and starts its own emulator
        test_src_build
        ;;
    interactive)
        echo "Starting interactive SSH session..."
        "$SCRIPT_DIR/test_ssh.exp" $PORT
        ;;
    *)
        echo "Unknown test: $TEST"
        echo "Available tests: basic, stat, all, src, interactive"
        exit 1
        ;;
esac

echo ""
echo "All tests completed!"
