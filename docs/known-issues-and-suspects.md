# Known Issues and Suspects — Cross-Repo Debugging Guide

This document describes known and suspected issues blocking `DataRetrievalTest.Gaussian1D` and the full measurement pipeline. Each issue lists the root cause, the files to inspect, and the specific code location.

---

## Issue 1: Hub built without `-tags falcon_core` (CONFIRMED BLOCKING)

**Status**: Active — blocking measurement pipeline entirely.

**Root cause**: The vcpkg portfile builds the hub Go binary without `-tags falcon_core`. The build compiles `falcon_core_stub.go` instead of `falcon_core.go`. The stub's parser looks for `parsedData["getters"]` — a plain-Go key that does not exist in cereal-format JSON. Every call to `ExtractGetters` / `ExtractSetters` returns an empty list.

**Repos affected**: `instrument-controller`, `falcon-instrument-hub`

**Files to inspect**:

| File | What to look for |
|------|-----------------|
| `instrument-controller/ports/falcon-instrument-hub/portfile.cmake` | `go build` line — must have `-tags falcon_core` |
| `falcon-instrument-hub/runtime/internal/serverinterpreter/falcon_core_stub.go` | `//go:build !falcon_core` tag — confirms stub is active when tag absent |
| `falcon-instrument-hub/runtime/internal/serverinterpreter/falcon_core.go` | `//go:build cgo && falcon_core` tag — only compiled when both flags set |

**Fix**: Change the `go build` line in `portfile.cmake`:
```cmake
# BEFORE
go build -o "${GO_OUTPUT}" ./cmd/main.go

# AFTER
go build -tags falcon_core -o "${GO_OUTPUT}" ./cmd/main.go
```
`PKG_CONFIG_PATH` is already set in the portfile; CGo will find `falcon-core-c-api.pc` at `vcpkg_installed/x64-linux-dynamic/lib/pkgconfig/`.

---

## Issue 2: `InterpreterDaemon` is never started — PROCESS_REQUEST goes nowhere (CONFIRMED BLOCKING)

**Status**: Active — measurement never executes.

**Root cause**: `measure_command_handler.go::sendProcessRequest()` publishes to NATS subject `PROCESS_REQUEST`. The `InterpreterDaemon` struct (`interpreter_daemon.go`) subscribes to this subject, but `NewInterpreterDaemon()` is **never called** from `cmd/main.go` or `setupHandlers()`. The message is sent to a subject with no subscribers. The measurement pipeline stalls permanently after the NATS publish.

**Repos affected**: `falcon-instrument-hub`

**Files to inspect**:

| File | What to look for |
|------|-----------------|
| `falcon-instrument-hub/runtime/internal/handlers/measure_command_handler.go` | `sendProcessRequest()` — publishes to `PROCESS_REQUEST`; `handleUploadData()` — waits for `UPLOAD_DATA` that never arrives |
| `falcon-instrument-hub/runtime/internal/serverinterpreter/interpreter_daemon.go` | `NewInterpreterDaemon` / `Start()` — dead code, never instantiated |
| `falcon-instrument-hub/runtime/cmd/main.go` | `setupHandlers()` — confirm no `NewInterpreterDaemon` call |
| `falcon-instrument-hub/runtime/external/falcon_runtime_harness/main.go` | `--start-dummy-backend` flag / `handleProcessRequest` — the only existing `PROCESS_REQUEST` subscriber; only present in harness binary, not in production |

**Fix**: Replace `sendProcessRequest` with a direct ISS HTTP `measure` RPC call. Parse `GET_DATAPOINT` floats from the response, build a `MeasurementResponse` via CGo, publish directly to `FALCON.MEASURE_RESPONSE`.

---

## Issue 3: Lua measurement script has wrong API arity and uses unretrievable buffer data (CONFIRMED BLOCKING)

**Status**: Active — even if the hub calls ISS, the Lua script will fail or produce no retrievable data.

**Root cause A — wrong argument order**: Generated Lua bindings require `(id, channel, ...)` where `id` is the ISS instrument instance name (e.g. `"MockInstrument1"`). The script currently passes only `(channel, ...)`, skipping `id`.

**Root cause B — `measureStream` result inaccessible**: `Mock5Meter1:measureStream(channel)` creates an in-process buffer inside ISS. The hub cannot retrieve buffer data via HTTP RPC. Only `GET_DATAPOINT` returns an inline float in the `collect_results_json()` / synchronous `measure` response.

**Repos affected**: `instrument-controller`

**Files to inspect**:

| File | What to look for |
|------|-----------------|
| `instrument-controller/tests/lua/measure_get_set.lua` | `Mock1Source1:setVoltage(setter.channel, voltage)` — missing `setter.id` first arg; `Mock5Meter1:measureStream(getter.channel)` — wrong data return path |
| `instrument-controller/tests/data-retrieval-1D/measurement-scripts/measure_get_set.tl` | Source Teal script — same bugs before compilation |
| `instrument-controller/tests/instrument-lua-libs/multimeter.lua` | `function Mock5Meter1:setSampleRate(id, channel, sample_rate)` — confirms `id` is first param |
| `instrument-controller/tests/instrument-lua-libs/source.lua` | `function Mock1Source1:setVoltage(id, channel, voltage)` — confirms `id` is first param |
| `instrument-script-server/src/plugins/mock-multimeter.c` | `GET_DATAPOINT` handler: returns `resp->return_value.value.d_val` inline; `MEASURE_STREAM` handler: creates `data_buffer_create()` — in-process only |

**Expected fixed script** (pseudocode):
```lua
Mock5Meter1:setSampleRate(getter.id, getter.channel, sampleRate)  -- id first
Mock5Meter1:setBins(getter.id, getter.channel, numPoints)
Mock1Source1:setVoltage(setter.id, setter.channel, voltage)        -- id first
for i = 1, numPoints do
    Mock5Meter1:getDatapoint(getter.id, getter.channel)            -- inline float, retrievable
end
```

---

## Issue 4: No `BuildMeasurementResponse` CGo function in hub (CONFIRMED BLOCKING)

**Status**: Active — even if ISS returns data, the hub cannot produce a valid falcon-core `MeasurementResponse`.

**Root cause**: The hub's CGo layer (`falcon_core.go`) has deserialization helpers for `MeasurementRequest` but no function to build a `MeasurementResponse` from raw float data. The `InterpreterDaemon::buildResponseJSON` produces simplified Go JSON, not the cereal-compatible format that `hub.cpp::from_json_string<MeasurementResponse>` requires on the controller side.

**Repos affected**: `falcon-instrument-hub`, `falcon-core-libs`

**Files to inspect**:

| File | What to look for |
|------|-----------------|
| `falcon-instrument-hub/runtime/internal/serverinterpreter/falcon_core.go` | Missing `BuildMeasurementResponse` function; existing imports to guide implementation |
| `falcon-instrument-hub/runtime/internal/serverinterpreter/falcon_core_stub.go` | Missing stub for `BuildMeasurementResponse` (needed so non-CGo builds compile) |
| `falcon-core-libs/go/falcon-core/communications/messages/measurementresponse/measurementResponse.go` | `measurementresponse.New(arrays)` — how to create handle |
| `falcon-core-libs/go/falcon-core/autotuner-interfaces/contexts/acquisitioncontext/acquisitionContext.go` | `acquisitioncontext.New(connection, instrumentType, units)` — key constructor |
| `falcon-core-libs/go/falcon-core/generic/farraydouble/fArrayDouble.go` | `farraydouble.FromData(data, shape)` — creates data array |
| `falcon-core-libs/go/falcon-core/math/arrays/labelledmeasuredarray/labelledMeasuredArray.go` | `labelledmeasuredarray.FromFArray(fa, ac)` — wraps data with context |
| `falcon-core-libs/go/falcon-core/communications/messages/measurementresponse/measurementResponse_test.go` | Working example of the full construction chain |

**Key construction chain** (reference `measurementResponse_test.go`):
```go
fa, _    := farraydouble.FromData([]float64{...}, []uint64{N})
conn, _  := connection.NewPlungerGate("P1")          // from setter
units, _ := symbolunit.NewMilliVolt()                // from getter
ac, _    := acquisitioncontext.New(conn, "VOLTMETER", units)
lma, _   := labelledmeasuredarray.FromFArray(fa, ac)
list, _  := listlabelledmeasuredarray.New([]*labelledmeasuredarray.Handle{lma})
arrays, _:= labelledarrayslabelledmeasuredarray.NewFromList(list)
resp, _  := measurementresponse.New(arrays)
json, _  := resp.ToJSON()                            // cereal-compatible
```

**Test expectations** (from `data-retrieval.cpp`):
- `labelledArray->connection()` = `Connection::PlungerGate("P1")` — from **setter** port
- `labelledArray->instrument_type()` = `InstrumentTypes::VOLTMETER` — from **getter** port
- `labelledArray->units()` = `SymbolUnit::MilliVolt()` — from **getter** port
- `labelledArray->size()` = 100

---

## Issue 5: NATS `MeasureResponse` `response` vs `stream` field ambiguity (SUSPECTED)

**Status**: Suspected future issue — not yet reached in current debugging.

**Root cause**: The controller (`routine_comms.cpp`) reads `json["response"]` from `FALCON.MEASURE_RESPONSE`. The hub's `api.MeasureResponse` struct publishes `{"response": "..."}`. These match. However, the `commands_definitions.hpp` `MeasureResponse` struct has a `stream` field, not `response`. The `routine_comms.cpp` comment says `// The hub publishes {"response": "..."}` which diverges from the generated struct. If the generated code is ever regenerated without manual edits, this will break.

**Files to inspect**:

| File | What to look for |
|------|-----------------|
| `falcon/comms/src/routine_comms.cpp` | `json["response"]` read at line ~37 |
| `falcon/comms/include/falcon-comms/commands_definitions.hpp` | `MeasureResponse` struct — has `stream` field but `routine_comms` reads `response` |
| `falcon-instrument-hub/runtime/internal/api/api.go` | `MeasureResponse` struct — `Response string json:"response"` |

---

## Issue 6: `commands_definitions.hpp` — `int timestamp` truncation (LOW RISK, PATCHED)

**Status**: Patched via portfile injection; may regress if portfile loses the fix.

**Root cause**: `MeasureCommand::timestamp` is `int` but caller uses `long long`. Silent truncation after ~2 billion microseconds of epoch time.

**Files to inspect**:

| File | What to look for |
|------|-----------------|
| `falcon/comms/include/falcon-comms/commands_definitions.hpp` | `int timestamp` in `MeasureCommand` and `MeasureResponse` |
| `instrument-controller/ports/falcon-comms/portfile.cmake` | Injection of fixed `.cpp` and `.hpp` overrides |

---

## Issue 7: `port_payload` serialization bypass in data-retrieval test (KNOWN DEBT)

**Status**: Known tech debt — test passes with manually constructed ports; real hub → controller port discovery not validated.

**Root cause**: `RuntimeComms::subscribe_port_payload` → `FALCON.PORT_PAYLOAD` deserialization of cereal-format `Ports` JSON is not yet stable end-to-end. The test constructs `InstrumentPort` objects manually to allow the rest of the pipeline to be tested.

**Files to inspect**:

| File | What to look for |
|------|-----------------|
| `instrument-controller/tests/data-retrieval-1D/data-retrieval.cpp` | `// TODO(port_payload)` comment near line 434; manually constructed `getter`, `independantKnob`, `clock` |
| `falcon-instrument-hub/runtime/internal/handlers/port_request_handler.go` | How hub serializes `InstrumentPort` list to `Ports` cereal JSON |
| `falcon/comms/src/runtime_comms.cpp` | `subscribe_port_payload` — how it deserializes `FALCON.PORT_PAYLOAD` response |
| `falcon-core-libs/go/falcon-core/instrument-interfaces/names/ports/ports.go` | `ports.ToJSON()` — must produce cereal-compatible output |
| `falcon-core-libs/go/falcon-core/generic/listinstrumentport/listInstrumentPort.go` | `New([])` fix (2026-05-08) — empty list no longer crashes |

---

## Issue 8: ISS instrument ID vs port name mapping in hub (SUSPECTED BLOCKING)

**Status**: Suspected — will block once ISS HTTP call path is implemented.

**Root cause**: When the hub calls ISS `measure`, it must pass `globals` containing per-instrument entries like `{"id": "MockInstrument1", "channel": 1}`. These must be derived by:
1. Calling `ExtractGetters` on the cereal MeasurementRequest → `DefaultName` = `"analog1_stream"`
2. Parsing the port name pattern `analogN_<type>` → channel N
3. Looking up the instrument YAML config to get ISS instance name (`name: MockInstrument1`)

The hub does not yet implement this mapping. The ISS instrument names come from the YAML `name:` field; the `DefaultName` in the falcon-core request is the port name, not the instrument name.

**Files to inspect**:

| File | What to look for |
|------|-----------------|
| `instrument-controller/tests/data-retrieval-1D/multimeter-config.yml` | `name: MockInstrument1`, `io_config.analog1_stream` — port name → instrument name mapping |
| `instrument-controller/tests/data-retrieval-1D/source-config.yml` | `name: MockInstrument2`, `io_config.analog4_voltage` |
| `falcon-instrument-hub/runtime/internal/serverinterpreter/falcon_core.go` | `ExtractGetters` → `DefaultName` field — this is the port name, not ISS instrument name |
| `falcon-instrument-hub/runtime/internal/handlers/measure_command_handler.go` | Where `BuildConfigurations()` / `instrumentHandler` is called — existing hook for per-instrument config |

**Mapping logic** (to implement):
- `"analog1_stream"` → instrument YAML where `io_config` has key `analog1_stream` → `name: MockInstrument1` → ISS ID `"MockInstrument1"`, channel `1`
- `"analog4_voltage"` → instrument YAML where `io_config` has key `analog4_voltage` → `name: MockInstrument2` → ISS ID `"MockInstrument2"`, channel `4`
- Channel number: regex `analog(\d+)_` on port name

---

## Issue 9: `setVoltages` map key must match ISS instrument ID, not port name (SUSPECTED)

**Status**: Suspected — will surface once Lua script receives ISS globals.

**Root cause**: The `measure_get_set.lua` script checks `if setter.id ~= "Source1"` and errors out. The `setVoltages` dict uses `setter.id` as a key. The ISS instrument name for the voltage source in the test config is `"MockInstrument2"`, not `"Source1"`. If the hub passes `setter.id = "MockInstrument2"`, the voltage lookup will fail. The Lua script guard and/or the globals dict must align.

**Files to inspect**:

| File | What to look for |
|------|-----------------|
| `instrument-controller/tests/lua/measure_get_set.lua` | `if setter.id ~= "Source1"` guard — hardcoded expected ID |
| `instrument-controller/tests/data-retrieval-1D/source-config.yml` | `name: MockInstrument2` — actual ISS instrument name |
| Hub `measure_command_handler.go` (to be written) | What value is placed in `setVoltages` key and `setters[].id` |

---

## Issue 10: `acquisitioncontext.New` instrument type string must match cereal-serialized form (SUSPECTED)

**Status**: Suspected — only relevant once `BuildMeasurementResponse` is implemented.

**Root cause**: `acquisitioncontext.New(conn, instrumentType, units)` takes `instrumentType` as a `string`. The test asserts `labelledArray->instrument_type() == InstrumentTypes::VOLTMETER`. The string representation used by cereal serialization must match what `InstrumentTypes::VOLTMETER` serializes to. Passing the wrong string will produce a `MeasurementResponse` that fails the type assertion.

**Files to inspect**:

| File | What to look for |
|------|-----------------|
| `falcon/comms/vcpkg/buildtrees/falcon-core/src/.../include/falcon-core/instrument_interfaces/names/InstrumentTypes.hpp` | How `VOLTMETER` is defined and serialized |
| `falcon-core-libs/go/falcon-core/communications/messages/measurementresponse/measurementResponse_test.go` | What string is passed as `instrument_type` in tests |
| `falcon-core-libs/go/falcon-core/instrument-interfaces/names/instrumentport/instrumentPort.go` | `InstrumentType()` method — returns whatever cereal stored |

---

## Quick reference: key file paths

### instrument-controller

```
ports/falcon-instrument-hub/portfile.cmake         # Hub build — needs -tags falcon_core
ports/falcon-instrument-hub/vcpkg.json             # port-version — bump to force rebuild
ports/instrument-script-server/portfile.cmake      # ISS build — injects InstrumentWorkerProxy.cpp
ports/falcon-comms/portfile.cmake                  # falcon-comms build — injects commands_definitions.hpp fix
tests/data-retrieval-1D/data-retrieval.cpp         # Integration test — manual port bypass (TODO)
tests/data-retrieval-1D/measurement-scripts/measure_get_set.tl  # Lua source — wrong arity, uses measureStream
tests/lua/measure_get_set.lua                      # Compiled Lua — active version used by ISS
tests/instrument-lua-libs/multimeter.lua           # Generated API: setSampleRate(id, channel, ...)
tests/instrument-lua-libs/source.lua               # Generated API: setVoltage(id, channel, ...)
tests/data-retrieval-1D/multimeter-config.yml      # ISS config: name=MockInstrument1, io_config ports
tests/data-retrieval-1D/source-config.yml          # ISS config: name=MockInstrument2, io_config ports
```

### falcon-instrument-hub

```
runtime/cmd/main.go                                          # Hub entrypoint — setupHandlers, startInstruments ordering
runtime/internal/handlers/measure_command_handler.go        # Core issue: sendProcessRequest → PROCESS_REQUEST void
runtime/internal/handlers/manager.go                        # Handler wiring
runtime/internal/serverinterpreter/falcon_core.go           # CGo path: ExtractGetters/Setters; needs BuildMeasurementResponse
runtime/internal/serverinterpreter/falcon_core_stub.go      # Stub: always active without -tags falcon_core
runtime/internal/serverinterpreter/interpreter_daemon.go    # Dead code: never instantiated from main
runtime/internal/serverinterpreter/client.go                # ISS HTTP RPC client: call(), SubmitMeasure(), etc.
runtime/internal/api/api.go                                  # MeasureCommand/MeasureResponse structs
```

### falcon-core-libs

```
go/falcon-core/cmemoryallocation/cmas.go                         # destroyOnce fix; FromCAPI no-cleanup ownership
go/falcon-core/generic/listinstrumentport/listInstrumentPort.go  # Empty list constructor fix
go/falcon-core/communications/messages/measurementresponse/      # Response construction
go/falcon-core/autotuner-interfaces/contexts/acquisitioncontext/ # acquisitioncontext.New(conn, type, units)
go/falcon-core/generic/farraydouble/                             # farraydouble.FromData(data, shape)
go/falcon-core/math/arrays/labelledmeasuredarray/                # labelledmeasuredarray.FromFArray(fa, ac)
go/falcon-core/physics/device-structures/connection/             # connection.NewPlungerGate, NewOhmic, etc.
go/falcon-core/physics/units/symbolunit/                         # symbolunit.NewMilliVolt, NewVolt, etc.
```

### instrument-script-server

```
src/server/InstrumentWorkerProxy.cpp   # Static destruction-order fix (heap singleton ProcessManager)
src/server/CommandHandlers.cpp         # measure/GET_DATAPOINT/MEASURE_STREAM C++ handlers
```

### falcon/comms

```
include/falcon-comms/commands_definitions.hpp   # MeasureCommand/MeasureResponse structs; int timestamp issue
include/falcon-comms/routine_comms.hpp          # subscribe_measure_response declaration
src/routine_comms.cpp                           # Publishes INSTRUMENTHUB.MEASURE_COMMAND; reads FALCON.MEASURE_RESPONSE["response"]
```
