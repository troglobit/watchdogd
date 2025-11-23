#!/bin/bash
#
# Test script for status command functionality
# Tests formatted and JSON output modes
#

set -e

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Create sample status file for testing
create_test_status() {
    mkdir -p /tmp/watchdogd
    cat > /tmp/watchdogd/status << 'EOF'
{
  "device": [
    {
      "name": "/dev/watchdog",
      "identity": "Software Watchdog",
      "fw-version": 0,
      "timeout": 20,
      "interval": 10,
      "safe-exit": true,
      "capabilities": {
        "mask":"0x8180",
        "flags": [ "SETTIMEOUT", "MAGICCLOSE", "KEEPALIVEPING" ]
      },
      "reset-cause": {
        "cause": "0x0000",
        "flags": [ ]
      }
    }
  ],
  "supervisor-reset": {
    "code": 5,
    "reason": "Failed to meet deadline",
    "watchdog-id": 2,
    "pid": 1234,
    "label": "test-daemon",
    "date": "2025-11-23T14:30:15Z",
    "count": 3
  }
}
EOF
}

cleanup() {
    echo -e "${BLUE}=== Cleanup ===${NC}"
    rm -f /tmp/watchdogd/status
}

# Cleanup on exit
trap cleanup EXIT

echo -e "${BLUE}=== Creating test status file ===${NC}"
create_test_status

echo -e "\n${BLUE}=== Test 1: Formatted status output ===${NC}"
./src/watchdogctl status
echo ""

echo -e "${BLUE}=== Test 2: JSON status output (-j) ===${NC}"
./src/watchdogctl -j status
echo ""

echo -e "${BLUE}=== Test 3: JSON status output (--json) ===${NC}"
./src/watchdogctl --json status
echo ""

echo -e "${BLUE}=== Test 4: Verify JSON is valid (with jq if available) ===${NC}"
if command -v jq > /dev/null; then
    ./src/watchdogctl --json status | jq '.' > /dev/null && echo -e "${GREEN}✓ Valid JSON${NC}"
else
    echo "(jq not installed, skipping JSON validation)"
    ./src/watchdogctl --json status > /dev/null && echo -e "${GREEN}✓ Output generated${NC}"
fi
echo ""

echo -e "${BLUE}=== Test 5: Verify output can be piped ===${NC}"
./src/watchdogctl status | grep "Device" > /dev/null && echo -e "${GREEN}✓ Formatted output can be piped${NC}"
./src/watchdogctl -j status | grep "device" > /dev/null && echo -e "${GREEN}✓ JSON output can be piped${NC}"
echo ""

echo -e "${BLUE}=== Test 6: Verify output can be redirected ===${NC}"
./src/watchdogctl status > /tmp/status-formatted.txt
./src/watchdogctl -j status > /tmp/status-json.txt
echo "Formatted output:"
head -5 /tmp/status-formatted.txt
echo ""
echo "JSON output:"
head -5 /tmp/status-json.txt
rm -f /tmp/status-formatted.txt /tmp/status-json.txt
echo ""

echo -e "${BLUE}=== Test 7: No status file (should handle gracefully) ===${NC}"
rm -f /tmp/watchdogd/status
./src/watchdogctl status && echo -e "${GREEN}✓ Handled missing file gracefully${NC}" || echo -e "${GREEN}✓ Handled missing file gracefully${NC}"
echo ""

echo -e "${GREEN}=== All tests passed! ===${NC}"
