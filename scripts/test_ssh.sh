#!/bin/bash
# SSH Test Harness for MP/M II Emulator
# Tests that SSH input and output actually work

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
EMU="$PROJECT_DIR/build/mpm2_emu"
DISK="$PROJECT_DIR/disks/mpm2_system.img"
PORT=2223  # Use different port to avoid conflicts
LOG="$PROJECT_DIR/test_ssh.log"
EXPECT_LOG="$PROJECT_DIR/test_ssh_expect.log"

cleanup() {
    pkill -f "mpm2_emu.*$PORT" 2>/dev/null || true
    rm -f "$LOG" "$EXPECT_LOG" 2>/dev/null || true
}

trap cleanup EXIT

echo "=== SSH Test Harness ==="
echo ""

# Check prerequisites
if [ ! -x "$EMU" ]; then
    echo "ERROR: Emulator not built. Run: cd build && cmake .. && make"
    exit 1
fi

if [ ! -f "$DISK" ]; then
    echo "ERROR: Disk image not found: $DISK"
    exit 1
fi

# Kill any existing instance
pkill -f "mpm2_emu.*$PORT" 2>/dev/null || true
sleep 1

# Start emulator
echo "Starting emulator on port $PORT..."
"$EMU" -d A:"$DISK" -p "$PORT" > "$LOG" 2>&1 &
EMU_PID=$!
sleep 3

# Check emulator is running
if ! kill -0 $EMU_PID 2>/dev/null; then
    echo "ERROR: Emulator failed to start"
    cat "$LOG"
    exit 1
fi

echo "Emulator running (PID $EMU_PID)"
echo ""

# Run expect script to test SSH
echo "Testing SSH connection..."
echo ""

expect -d << 'EXPECT_SCRIPT' 2>"$EXPECT_LOG"
set timeout 10

# Connect via SSH
spawn ssh -tt -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p 2223 test@localhost

# Wait for MP/M II banner or prompt
expect {
    -re "MP/M II" {
        puts "\n>>> OUTPUT TEST: Got MP/M II banner"
    }
    timeout {
        puts "\n>>> OUTPUT TEST FAILED: No MP/M II banner within 10 seconds"
        exit 1
    }
    eof {
        puts "\n>>> OUTPUT TEST FAILED: Connection closed unexpectedly"
        exit 1
    }
}

# Wait for the prompt (e.g., "0A>" or "3A>")
expect {
    -re "\[0-9\]A>" {
        puts ">>> OUTPUT TEST: Got command prompt"
    }
    timeout {
        puts ">>> OUTPUT TEST FAILED: No command prompt within 10 seconds"
        exit 1
    }
}

# Send a command
puts ">>> INPUT TEST: Sending 'dir' command"
send "dir\r"

# Wait for directory output or prompt return
expect {
    -re "A:|Directory|No File|\[0-9\]A>" {
        puts ">>> INPUT TEST: Got response to dir command"
    }
    timeout {
        puts ">>> INPUT TEST FAILED: No response to dir command"
        exit 1
    }
}

# Wait for prompt again
expect {
    -re "\[0-9\]A>" {
        puts ">>> SUCCESS: Both input and output working!"
    }
    timeout {
        puts ">>> PARTIAL: Got some output but no final prompt"
    }
}

# Clean exit
send "\x03"
expect eof
EXPECT_SCRIPT

RESULT=$?

echo ""
echo "=== Emulator Log ==="
cat "$LOG"
echo ""

if [ $RESULT -eq 0 ]; then
    echo "=== TEST PASSED ==="
else
    echo "=== TEST FAILED ==="
    echo ""
    echo "Expect debug log:"
    cat "$EXPECT_LOG" 2>/dev/null | tail -50
fi

exit $RESULT
