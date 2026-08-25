# =============================================================================
# ISS Smoke Test (Windows PowerShell)
# Tests that instrument-script-server can:
#   1. Start the daemon
#   2. Load a mock instrument plugin
#   3. Run a Lua measurement script against it
#   4. Clean up
#
# Usage (from repo root, in PowerShell):
#   .\tests\iss-smoke-test.ps1
# =============================================================================

$ErrorActionPreference = "Stop"

$RepoRoot  = Resolve-Path "$PSScriptRoot\.."
$ISS       = "$RepoRoot\vcpkg_installed\x64-windows-dynamic\tools\instrument-script-server\instrument-script-server.exe"
$Plugin    = "$RepoRoot\build\windows-clang-release\tests\mock_multimeter.dll"
$Config    = "$RepoRoot\tests\instrument-control\multimeter-config-direct.yml"
$Script    = "$RepoRoot\tests\smoke-measure.lua"

$Pass = 0
$Fail = 0

function Ok($msg)   { Write-Host "  [PASS] $msg" -ForegroundColor Green; $script:Pass++ }
function Fail($msg) { Write-Host "  [FAIL] $msg" -ForegroundColor Red;   $script:Fail++ }
function Step($msg) { Write-Host; Write-Host ">>> $msg" -ForegroundColor Cyan }

# ---------------------------------------------------------------------------
Step "Checking prerequisites..."

if (Test-Path $ISS)    { Ok "instrument-script-server.exe found" }
else                   { Fail "Binary not found at: $ISS"; exit 1 }

if (Test-Path $Plugin) { Ok "mock_multimeter.dll found" }
else                   { Fail "Plugin not found at: $Plugin (run 'make build' first)"; exit 1 }

if (Test-Path $Config) { Ok "Config file found" }
else                   { Fail "Config not found at: $Config"; exit 1 }

if (Test-Path $Script) { Ok "Lua script found" }
else                   { Fail "Script not found at: $Script"; exit 1 }

# ---------------------------------------------------------------------------
Step "Stopping any stale daemon..."
& $ISS daemon stop 2>$null
Start-Sleep -Seconds 1

# ---------------------------------------------------------------------------
Step "Starting daemon..."
& $ISS daemon start --log-level warn
Start-Sleep -Seconds 1
$status = & $ISS daemon status 2>&1
if ($status -match "running") {
    Ok "Daemon started: $status"
} else {
    Fail "Daemon failed to start: $status"
    exit 1
}

# ---------------------------------------------------------------------------
Step "Starting mock multimeter instrument..."
& $ISS inst start $Config --plugin $Plugin --log-level warn
Start-Sleep -Seconds 1

$instStatus = & $ISS inst status Meter1 2>&1
if ($instStatus -match "running|ok|Meter1") {
    Ok "Instrument Meter1 is running"
} else {
    Fail "Instrument status unexpected: $instStatus"
}

# ---------------------------------------------------------------------------
Step "Running measurement script..."
$measureOut = & $ISS measure $Script 2>&1
Write-Host $measureOut

if ($measureOut -match "Measurement complete") {
    Ok "Measurement completed successfully"
} else {
    Fail "Measurement did not complete as expected"
}

# ---------------------------------------------------------------------------
Step "Stopping instrument..."
& $ISS inst stop Meter1 2>$null
if ($?) { Ok "Instrument stopped" } else { Fail "Failed to stop instrument" }

Step "Stopping daemon..."
& $ISS daemon stop 2>$null
if ($?) { Ok "Daemon stopped" } else { Fail "Failed to stop daemon" }

# ---------------------------------------------------------------------------
Write-Host
Write-Host "==============================" -ForegroundColor White
Write-Host " Results: $Pass passed, $Fail failed" -ForegroundColor $(if ($Fail -eq 0) { "Green" } else { "Red" })
Write-Host "==============================" -ForegroundColor White

exit $(if ($Fail -eq 0) { 0 } else { 1 })
