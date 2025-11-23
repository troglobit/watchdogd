#!/bin/bash
#
# Test script for list-clients functionality
# Tests that watchdogctl list-clients outputs to stdout instead of syslog
#

set -e

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m' # No Color

cleanup() {
    echo -e "${BLUE}=== Cleanup ===${NC}"
    pkill -9 ex1 2>/dev/null || true
    pkill -9 ex2 2>/dev/null || true
    pkill -9 watchdogd 2>/dev/null || true
    rm -f /tmp/test-watchdogd.conf
    sleep 1
}

# Cleanup on exit
trap cleanup EXIT

echo -e "${BLUE}=== Building watchdogd ===${NC}"
make -j$(nproc) all

echo -e "${BLUE}=== Creating test configuration ===${NC}"
cat > /tmp/test-watchdogd.conf << 'EOF'
supervisor {
    enabled  = true
}
EOF

echo -e "${BLUE}=== Starting watchdogd in test mode ===${NC}"
./src/watchdogd -S -n -l debug -f /tmp/test-watchdogd.conf > /tmp/watchdogd.log 2>&1 &
DAEMON_PID=$!
sleep 2

# Check if daemon is running
if ! ps -p $DAEMON_PID > /dev/null; then
    echo -e "${RED}Failed to start watchdogd daemon!${NC}"
    cat /tmp/watchdogd.log
    exit 1
fi

echo -e "${GREEN}✓ Daemon started (PID: $DAEMON_PID)${NC}"

echo -e "\n${BLUE}=== Test 0: Status ===${NC}"
./src/watchdogctl
echo ""

echo -e "\n${BLUE}=== Test 1: No clients ===${NC}"
./src/watchdogctl list-clients
echo ""

echo -e "${BLUE}=== Test 2: One client (ex1) ===${NC}"
./examples/ex1 > /tmp/ex1.log 2>&1 &
EX1_PID=$!
sleep 2
echo -e "${GREEN}✓ Started ex1 (PID: $EX1_PID)${NC}"
./src/watchdogctl list-clients
echo ""

echo -e "${BLUE}=== Test 3: Two clients (ex1 + ex2) ===${NC}"
./examples/ex2 > /tmp/ex2.log 2>&1 &
EX2_PID=$!
sleep 2
echo -e "${GREEN}✓ Started ex2 (PID: $EX2_PID)${NC}"
./src/watchdogctl list-clients
echo ""

echo -e "${BLUE}=== Test 4: Verify output can be piped ===${NC}"
echo "Piping to grep for 'ex1':"
./src/watchdogctl list-clients | grep ex1
echo ""

echo -e "${BLUE}=== Test 5: Verify output can be redirected ===${NC}"
./src/watchdogctl list-clients > /tmp/clients.txt
echo "Output saved to /tmp/clients.txt:"
cat /tmp/clients.txt
echo ""

echo -e "${BLUE}=== Test 6: JSON format with -j ===${NC}"
./src/watchdogctl -j list-clients
echo ""

echo -e "${BLUE}=== Test 7: JSON format with --json ===${NC}"
./src/watchdogctl --json list-clients
echo ""

echo -e "${BLUE}=== Test 8: JSON output can be piped to jq ===${NC}"
echo "Piping JSON to jq (if available):"
if command -v jq > /dev/null; then
    ./src/watchdogctl --json list-clients | jq '.'
else
    echo "(jq not installed, skipping)"
    ./src/watchdogctl --json list-clients
fi
echo ""

echo -e "${BLUE}=== Test 9: No clients with JSON format ===${NC}"
pkill -9 ex1 ex2 2>/dev/null || true
sleep 1
./src/watchdogctl --json list-clients
echo ""

echo -e "${GREEN}=== All tests passed! ===${NC}"
