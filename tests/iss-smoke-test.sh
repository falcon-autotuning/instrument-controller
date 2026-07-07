#!/usr/bin/env bash
# =============================================================================
# ISS Smoke Test
# Tests that instrument-script-server can:
#   1. Start the daemon
#   2. Load the mock VISA instrument plugin
#   3. Run a Lua measurement script against it
#   4. Clean up
#
# Usage (from repo root):
#   bash tests/iss-smoke-test.sh
# =============================================================================

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DAEMON="$REPO_ROOT/vcpkg_installed/x64-linux-dynamic/tools/instrument-script-server/instrument-script-server-daemon"
CLI="$REPO_ROOT/vcpkg_installed/x64-linux-dynamic/tools/instrument-script-server/instrument-script-server"
PLUGIN="$REPO_ROOT/vcpkg_installed/x64-linux-dynamic/lib/instrument-plugins/visa_plugin.so"
CONFIG="$REPO_ROOT/../instrument-script-server/tests/data/mock_instrument1.yaml"
SCRIPT="$REPO_ROOT/tests/smoke-measure.lua"

PASS=0
FAIL=0

ok()   { echo "  [PASS] $*"; PASS=$((PASS+1)); }
fail() { echo "  [FAIL] $*"; FAIL=$((FAIL+1)); }
step() { echo; echo ">>> $*"; }

# ---------------------------------------------------------------------------
step "Checking prerequisites..."

[[ -x "$DAEMON" ]] && ok "Daemon binary found" || { fail "Daemon not found at: $DAEMON"; exit 1; }
[[ -x "$CLI" ]]    && ok "CLI binary found" || { fail "CLI not found at: $CLI"; exit 1; }
[[ -f "$PLUGIN" ]] && ok "VISA plugin found" || { fail "Plugin not found at: $PLUGIN"; exit 1; }
[[ -f "$CONFIG" ]] && ok "Config file found" || { fail "Config not found at: $CONFIG"; exit 1; }
[[ -f "$SCRIPT" ]] && ok "Lua script found" || { fail "Script not found at: $SCRIPT"; exit 1; }

# ---------------------------------------------------------------------------
step "Stopping any stale daemon..."
pkill -f "instrument-script-server-daemon" || true
sleep 1

# ---------------------------------------------------------------------------
step "Starting daemon..."
"$DAEMON" --log-level warn &
DAEMON_PID=$!
sleep 2

STATUS=$("$CLI" daemon status 2>&1)
if echo "$STATUS" | grep -q "running"; then
    ok "Daemon started (pid=$DAEMON_PID): $STATUS"
else
    fail "Daemon failed to start: $STATUS"
    kill $DAEMON_PID 2>/dev/null || true
    exit 1
fi

# Ensure cleanup on exit
trap '"$CLI" inst stop MockInstrument1 2>/dev/null || true; kill $DAEMON_PID 2>/dev/null || true' EXIT

# ---------------------------------------------------------------------------
step "Starting mock VISA instrument..."
"$CLI" inst start "$CONFIG" --plugin "$PLUGIN" --log-level warn
sleep 1

INST_STATUS=$("$CLI" inst status MockInstrument1 2>&1)
if echo "$INST_STATUS" | grep -qi "running\|ok\|MockInstrument1"; then
    ok "Instrument MockInstrument1 is running"
else
    fail "Instrument status unexpected: $INST_STATUS"
fi

# ---------------------------------------------------------------------------
step "Running measurement script..."
MEASURE_OUT=$("$CLI" measure "$SCRIPT" 2>&1)
echo "$MEASURE_OUT"

if echo "$MEASURE_OUT" | grep -q "Measurement complete"; then
    ok "Measurement completed successfully"
else
    fail "Measurement did not complete as expected"
fi

# ---------------------------------------------------------------------------
step "Stopping instrument..."
"$CLI" inst stop MockInstrument1 2>/dev/null && ok "Instrument stopped" || fail "Failed to stop instrument"

step "Stopping daemon..."
kill $DAEMON_PID 2>/dev/null && ok "Daemon stopped" || fail "Failed to stop daemon"

# Disarm trap since we already cleaned up
trap - EXIT

# ---------------------------------------------------------------------------
echo
echo "=============================="
echo " Results: $PASS passed, $FAIL failed"
echo "=============================="

[[ $FAIL -eq 0 ]] && exit 0 || exit 1
