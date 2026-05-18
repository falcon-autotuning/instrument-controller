# Cross-Repo Alignment Blueprint
**Goal**: Get `make test` passing for `DataRetrievalTest.Gaussian1D` in `instrument-controller`.

**Date**: 2026-05-08  
**Constraints**:
- `falcon-core` — **NO changes**. Use as-is.
- `falcon-core-libs` — Minimal changes only (fixes already merged: `cmas.go`, `listInstrumentPort.go`).
- `instrument-script-server (ISS)` — Minimal changes only (fix already merged: heap-backed singleton in `InstrumentWorkerProxy.cpp` v1.1.7).
- `falcon-instrument-hub` — Primary target for alignment. Align subjects, startup reliability, and HDF5 write path.
- `falcon/comms` — May be changed to align the measurement response transport (see Phase 4b).
- `instrument-controller/tests` — Test harness improvements only (no test-logic changes).

---

## Architecture Overview

```
[instrument-controller test]
      │  NATS
      │  publish: INSTRUMENTHUB.MEASURE_COMMAND  (via falcon-comms RoutineComms)
      │  subscribe: FALCON.MEASURE_RESPONSE      (via falcon-comms RoutineComms)
      │
[falcon-instrument-hub]  (Go binary, embedded NATS server)
      │  calls ISS RPC on 127.0.0.1:<port>
      │
[instrument-script-server daemon]  (C++ binary)
      │  spawns instrument-worker processes via posix_spawnp
      │
[instrument-worker]  (C++ binary, loads .so plugin, Boost IPC message queues)
```

**HDF5 write path** (target state):
- Hub receives `UPLOAD_DATA` with measurement JSON
- Hub constructs `hdf5data.Handle` via `hdf5data.NewFromCommunications()` (falcon-core Go bindings)
- Hub calls `handle.ToFile(path)` to write the HDF5 file
- Hub registers measurement in SQLite via `measurements.Manager`
- Hub publishes `FALCON.MEASURE_RESPONSE` with inline `MeasurementResponse` JSON

---

## Phases

### Phase 0 — Diagnosis ✅ COMPLETE
**Finding**: Stale ISS daemon persists between test runs because `TearDown()` only sent
`SIGTERM` to the hub process without waiting for it to exit. The hub's `defer cleanup()`
calls `stopISSDaemon()`, but since no `waitpid()` was called, the test could re-run
before the daemon stopped, causing "Instrument already exists" on the second run.

---

### Phase 1 — Fix Stale ISS Daemon ✅ COMPLETE
**Files changed**:
- `instrument-controller/tests/data-retrieval-1D/data-retrieval.cpp`
  - Added `#include <sys/wait.h>`
  - Added `waitpid(hub_pid_, &status, 0)` in `StopInstrumentHub()` after `kill(SIGTERM)`

**Result**: Test now gets past instrument creation and reaches `request_measurement()`.
The failure is now `Timeout waiting for MeasureResponse` (NATS subject mismatch — see Phase 4).

---

### Phase 2a — ISS Readiness Polling in Hub
**File**: `falcon-instrument-hub/runtime/cmd/main.go` — `startISSDaemon()`

**Problem**: `startISSDaemon()` does `time.Sleep(500ms)` then returns. If the ISS daemon
takes longer to bind its HTTP port, subsequent `startInstruments()` RPC calls fail.

**Fix**: Replace the fixed sleep with a polling loop:
```go
// After cmd.Start(), replace time.Sleep(500*time.Millisecond) with:
deadline := time.Now().Add(5 * time.Second)
rpcPort := instrumentServerPort
if rpcPort <= 0 {
    rpcPort = 8555
}
client := serverinterpreter.NewScriptServerClient("127.0.0.1", rpcPort)
for time.Now().Before(deadline) {
    if _, err := client.ListInstruments(); err == nil {
        break
    }
    time.Sleep(100 * time.Millisecond)
}
```
If `ListInstruments` (or equivalent ping) is not available, use a TCP dial to
`127.0.0.1:<rpcPort>` as the readiness probe.

**After**: Bump `port-version` in `instrument-controller/ports/falcon-instrument-hub/vcpkg.json`.

---

### Phase 2b — Fatal on startInstruments Failure
**File**: `falcon-instrument-hub/runtime/cmd/main.go` — `runStart()`

**Problem**: If `startInstruments()` fails, the hub logs a warning and continues with no
instruments registered. The test then times out waiting for measurement data.

**Fix**:
```go
// Change:
if err := startInstruments(); err != nil {
    log.Printf("warning: failed to start instruments: %v", err)
}
// To:
if err := startInstruments(); err != nil {
    return fmt.Errorf("failed to start instruments: %w", err)
}
```
The hub process must exit non-zero if instruments cannot be registered, so the test
`SetUp()` fails fast rather than hanging for 5 seconds.

**After**: Bump `port-version` in `instrument-controller/ports/falcon-instrument-hub/vcpkg.json`.

---

### Phase 3 — WaitForInstruments in Test
**File**: `instrument-controller/tests/data-retrieval-1D/data-retrieval.cpp` — `SetUp()`

**Problem**: `SetUp()` calls `WaitForNats()` (TCP port 4222 check only) and then
immediately runs the test. However, instruments may not have sent `CONFIRM_INITIALIZATION`
yet, so `request_config()` or `request_measurement()` may race against ISS startup.

**Fix**: After `WaitForNats()`, add a `WaitForInstruments()` helper that:
1. Subscribes to `CONFIRM_INITIALIZATION.*` NATS wildcard.
2. Waits until 2 messages arrive (MockInstrument1 + MockInstrument2), or times out (15s).
3. Uses the existing NATS C++ client from `falcon-comms` or a lightweight raw NATS connection.

```cpp
void WaitForInstruments(int expected_count, int timeout_ms) {
    // subscribe CONFIRM_INITIALIZATION.*
    // count messages until expected_count or timeout
}
```

---

### Phase 4 — NATS Subject and Response Format Alignment ← CURRENT BLOCKER
#### 4a — Subscribe Subject Fix
**File**: `falcon-instrument-hub/runtime/internal/handlers/measure_command_handler.go`

**Problem**: Hub subscribes on `MEASURE_COMMAND.external.>`.  
**Controller publishes on**: `INSTRUMENTHUB.MEASURE_COMMAND` (from `falcon/comms/src/routine_comms.cpp`).

**Fix**: Change constants:
```go
// Change:
MeasureCommandSubject = "MEASURE_COMMAND.external"
MeasureResponseSubject = "MEASURE_RESPONSE.external"
// To:
MeasureCommandSubject = "INSTRUMENTHUB.MEASURE_COMMAND"
MeasureResponseSubject = "FALCON.MEASURE_RESPONSE"
```
Change subscription from `MeasureCommandSubject+".>"` to `MeasureCommandSubject`.

The `name` extracted from the subject (used as routing key for the response) is no longer
available in the flat subject `INSTRUMENTHUB.MEASURE_COMMAND`. Use the `Hash` field from
`MeasureCommand` as the correlation key instead — or remove the name-based routing since
the response goes on a single `FALCON.MEASURE_RESPONSE` subject the controller subscribes to.

#### 4b — Response Payload Format Fix
**Problem**: Hub sends `FALCON.MEASURE_RESPONSE` (after 4a) with payload:
```json
{"response": "<MeasurementResponse JSON>", "timestamp": 123, "hash": 456}
```
Controller's `falcon-comms` (`commands_definitions.hpp`) expects:
```json
{"stream": "...", "channel": "...", "timestamp": 123}
```
Then calls `hub_.jetstream_pull(stream, channel, 1)` to fetch the actual measurement data.

**Recommended Fix (inline response, no JetStream)**:
Change `falcon/comms/src/routine_comms.cpp` `subscribe_measure_response` to parse the
inline `response` field instead of using JetStream:

```cpp
// In subscribe_measure_response callback, parse inline:
auto json = nlohmann::json::parse(data);
// Read json["response"] as the MeasurementResponse JSON string
MeasureResponse response;
response.stream = json.value("response", "");  // reuse stream field as carrier
prom.set_value(response);
```
Then in `hub.cpp` `request_measurement`, use `resp.stream` directly as the serialized
`MeasurementResponse` instead of doing `pull_measurement_data`:
```cpp
// Instead of:
auto outs = comms.pull_measurement_data(resp.stream, resp.channel, 1);
return MeasurementResponse::from_json_string<...>(outs[0]);
// Use:
return MeasurementResponse::from_json_string<...>(resp.stream);
```

This avoids implementing NATS JetStream infrastructure while maintaining correct data flow.
The `falcon-comms` structs can keep their names (`stream`, `channel`) or be renamed.

**Alternative (full JetStream)**: Hub pushes measurement JSON to a JetStream subject and
returns `{stream, channel}` in `FALCON.MEASURE_RESPONSE`. Requires JetStream stream
creation in hub startup and publish-to-subject in `handleUploadData`. Higher complexity;
defer until JetStream is needed for large/buffered measurements.

**After**: Bump `port-version` in `instrument-controller/ports/falcon-instrument-hub/vcpkg.json`.

---

### Phase 5 — Hub HDF5 Write Path: Use falcon-core Go Bindings
**Context**: The hub currently has a stub `measurements/hdf5.go` with homegrown Go structs
(`HDF5Metadata`, `HDF5Data`, etc.) that are never fully implemented (file is a TODO stub).
The actual write path goes through `manager.go` which only tracks file paths in SQLite —
there is no actual HDF5 write in the hub today.

The `falcon-core-libs/go/falcon-core/communications/hdf5data/hDF5Data.go` package already
wraps the C API for reading and writing HDF5 files. It provides:
- `hdf5data.NewFromCommunications(request, response, voltageStates, sessionID, title, id, ts)` — construct from falcon-core comms types
- `hdf5data.NewFromFile(path)` — read from file
- `handle.ToFile(path)` — write to HDF5 file
- `handle.ToCommunications()` — extract `MeasurementResponse` + `MeasurementRequest`
- `handle.ToJSON()` / `hdf5data.FromJSON(json)` — JSON round-trip

**Required changes in `falcon-instrument-hub`**:

1. **Delete `measurements/hdf5.go`** — the stub types are unused and replaced by the Go binding.

2. **Add import** of `hdf5data` package in the hub's Go module:
   ```go
   import "github.com/falcon-autotuning/falcon-core-libs/go/falcon-core/communications/hdf5data"
   ```
   The module is already available via the `replace` directive in `runtime/go.mod`.

3. **In `handleUploadData` (measure_command_handler.go)**:
   - The `UPLOAD_DATA.Data` field contains the serialized `MeasurementResponse` JSON.
   - Construct the HDF5 file using `hdf5data.FromJSON(uploadData.Data)` OR, once the
     full `MeasurementRequest` + `MeasurementResponse` + `VoltageStates` are in scope,
     use `hdf5data.NewFromCommunications(...)`.
   - Write to the pre-allocated path via `handle.ToFile(expectedPath)`.
   - Call `measurementManager.CompleteMeasurement(uniqueID, title, expectedPath)`.

4. **CGo build requirements**: The hub's portfile (`instrument-controller/ports/falcon-instrument-hub/portfile.cmake`) already sets `PKG_CONFIG_PATH` and `CGO_LDFLAGS` to find `falcon-core-c-api`. Verify that `falcon-core-c-api.pc` is present and that `HDF5Data_c_api.h` is in the include path.

5. **Remove `measurements/hdf5.go`** stub file (it conflicts with the real types from the binding).

**Relationship to Phase 4b**: The measurement data delivered via `FALCON.MEASURE_RESPONSE`
should ultimately be the `MeasurementResponse` JSON produced by the hub after writing the
HDF5 file. The HDF5 file is written by the hub in `handleUploadData`, and the serialized
`MeasurementResponse` JSON (from `hdf5data.Handle.ToCommunications()`) is what the
controller deserializes on the other end.

---

### Phase 6 — PORT_PAYLOAD Protocol (Future / Deferred)
**Context**: The test currently constructs `InstrumentPort` objects manually with a
`// TODO(port_payload)` comment. The intended flow is:
1. Hub publishes port metadata on `FALCON.PORT_PAYLOAD` after instruments initialize.
2. Controller calls `request_port_payload()` → `RuntimeComms::subscribe_port_payload()`.
3. `InstrumentPort` objects are deserialized from JSON using falcon-core types.

**Defer until**: Phases 1–5 are complete and the basic measurement pipeline passes.

**Files to change when implementing**:
- Hub: `port_request_handler.go` — verify it publishes on `FALCON.PORT_PAYLOAD` with
  the correct `knobs`/`meters` JSON serialization matching `Ports::to_json_string()`.
- Test: Replace manual port construction with `request_port_payload(TIMEOUT_MS)`.

---

## Subject Reference Table

| Command | Publisher | Subject | Subscriber |
|---|---|---|---|
| DEVICE_CONFIG_REQUEST | controller (falcon-comms) | `INSTRUMENTHUB.DEVICE_CONFIG_REQUEST` | hub ✅ |
| DEVICE_CONFIG_RESPONSE | hub | `FALCON.DEVICE_CONFIG_RESPONSE` | controller ✅ |
| PORT_REQUEST | controller (falcon-comms) | `INSTRUMENTHUB.PORT_REQUEST` | hub ✅ |
| PORT_PAYLOAD | hub | `FALCON.PORT_PAYLOAD` | controller (RuntimeComms) |
| MEASURE_COMMAND | controller (falcon-comms) | `INSTRUMENTHUB.MEASURE_COMMAND` | hub ❌ (currently `MEASURE_COMMAND.external.>`) |
| MEASURE_RESPONSE | hub | `FALCON.MEASURE_RESPONSE` | controller ❌ (currently `MEASURE_RESPONSE.external.<name>`) |
| SETUP_INSTRUMENT | hub→ISS | `SETUP_INSTRUMENT.external.*` | ISS workers |
| CONFIRM_INITIALIZATION | ISS→hub | `CONFIRM_INITIALIZATION.*` | hub |
| MEASUREMENT_READY | ISS→hub | `MEASUREMENT_READY` | hub |
| RETURN_DATA | ISS→hub | `RETURN_DATA` | hub |
| UPLOAD_DATA | ISS→hub | `UPLOAD_DATA` | hub |

---

## File Change Summary by Phase

### Phase 2a (ISS readiness polling):
- `falcon-instrument-hub/runtime/cmd/main.go` — `startISSDaemon()`
- `instrument-controller/ports/falcon-instrument-hub/vcpkg.json` — bump `port-version`

### Phase 2b (fatal startInstruments):
- `falcon-instrument-hub/runtime/cmd/main.go` — `runStart()`
- `instrument-controller/ports/falcon-instrument-hub/vcpkg.json` — bump `port-version`

### Phase 3 (WaitForInstruments):
- `instrument-controller/tests/data-retrieval-1D/data-retrieval.cpp` — add `WaitForInstruments()`

### Phase 4a (subject fix):
- `falcon-instrument-hub/runtime/internal/handlers/measure_command_handler.go`
  - `MeasureCommandSubject = "INSTRUMENTHUB.MEASURE_COMMAND"`
  - `MeasureResponseSubject = "FALCON.MEASURE_RESPONSE"`
  - Remove name-based routing suffix; use Hash for correlation
- `instrument-controller/ports/falcon-instrument-hub/vcpkg.json` — bump `port-version`

### Phase 4b (response format fix):
- `falcon/comms/src/routine_comms.cpp` — parse inline `response` field, skip JetStream
- `instrument-controller/vcpkg/buildtrees/falcon-routine/` — NOTE: the installed falcon-routine binary is already built; the portfile may need to rebuild `falcon-comms` too (check `instrument-controller/ports/`)
- OR add an overlay port for `falcon-comms` if it is managed via vcpkg

### Phase 5 (HDF5 via falcon-core Go bindings):
- `falcon-instrument-hub/runtime/internal/measurements/hdf5.go` — delete stub, replace with real implementation using `hdf5data` Go binding
- `falcon-instrument-hub/runtime/internal/handlers/measure_command_handler.go` — add HDF5 write in `handleUploadData`
- `instrument-controller/ports/falcon-instrument-hub/vcpkg.json` — bump `port-version`

---

## Rebuild Trigger (vcpkg overlay ports)
After any hub Go source change:
1. Bump `port-version` in `instrument-controller/ports/falcon-instrument-hub/vcpkg.json`
2. Run `make test` — vcpkg detects version change and rebuilds the hub binary

After any C++ source change affecting falcon-comms or falcon-routine:
1. Bump port-version in the relevant overlay port under `instrument-controller/ports/`
2. Run `make test`

---

## Current Status (as of 2026-05-08 21:23)
| Phase | Status | Notes |
|---|---|---|
| 0 — Diagnosis | ✅ Done | Root cause: stale ISS daemon, instruments not re-created |
| 1 — Stale daemon fix | ✅ Done | `waitpid` in `StopInstrumentHub()` |
| 2a — ISS readiness | ⏳ Pending | 500ms sleep is fragile; polling needed |
| 2b — Fatal instruments | ⏳ Pending | Warning-and-continue hides failures |
| 3 — WaitForInstruments | ⏳ Pending | Race between NATS up and instruments ready |
| 4a — Subject fix (cmd) | ⏳ Pending | **Current blocker** — wrong subscribe subject |
| 4b — Response format | ⏳ Pending | JetStream vs inline data mismatch |
| 5 — HDF5 falcon-core | ⏳ Pending | Stub types → real Go binding |
| 6 — PORT_PAYLOAD | 🔮 Deferred | Not needed for initial test pass |
