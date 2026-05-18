# Hub Runtime Debug Changes (2026-05-08)

This note summarizes the key cross-repo changes made while debugging `make test` failures around the hub runtime, vcpkg packaging, and integration startup.

## Scope

- Primary failing test: `DataRetrievalTest.Gaussian1D`
- Main symptoms observed over time:
  - vcpkg overlay build failures for hub port
  - Go/CGo linker failures (transitive shared libs)
  - runtime crash: `munmap_chunk(): invalid pointer`
  - protocol mismatch in port payload serialization
  - current blocker: instrument startup segfault in ISS `start` flow

## Repo: instrument-controller

### 1) Overlay port hardening for hub build

File: `ports/falcon-instrument-hub/portfile.cmake`

Key changes:

- Updated source pin to `v1.0.8` tarball hash.
- Added explicit `PKG_CONFIG_PATH` to vcpkg-installed pkg-config dir.
- Added `CGO_LDFLAGS` with:
  - `-L<installed lib dir>`
  - `-Wl,-rpath-link,<installed lib dir>`
- Added `go mod edit -replace` rewrite in build tree so:
  - `github.com/falcon-autotuning/falcon-core-libs/go/falcon-core`
  - resolves to absolute workspace path during vcpkg builds.

Why:

- Fix invalid relative replace path inside vcpkg buildtree.
- Resolve CGo link failures against transitive dependencies.

### 2) Overlay port metadata updates

File: `ports/falcon-instrument-hub/vcpkg.json`

Key changes:

- Version bumped to `1.0.8`.
- `port-version` incremented multiple times to force rebuilds while iterating fixes.

Why:

- Ensure updated runtime/build logic is actually rebuilt and installed by vcpkg.

### 3) Makefile shell portability fix (existing local change in repo)

File: `Makefile`

Key changes:

- Set `SHELL ?= /bin/sh`.
- Replaced `source ...` with POSIX `. ...` in `test` target.

Why:

- Improve portability for non-bash shells and CI/sh environments.

## Repo: falcon-core-libs

### 1) Memory cleanup ownership fix in C wrapper layer

File: `go/falcon-core/cmemoryallocation/cmas.go`

Key changes:

- Added idempotent destroy tracking (`destroyOnce`) using a guarded pointer map.
- `CloseAllocation` now uses idempotent destroy path.
- `NewAllocation` cleanup now uses `destroyOnce`.
- **Important ownership change:** `FromCAPI` no longer registers runtime cleanup.

Why:

- `FromCAPI` often wraps borrowed pointers from C++ getters.
- Auto-destroying borrowed pointers caused invalid free (`munmap_chunk`) crashes.

### 2) Empty list constructor fix

File: `go/falcon-core/generic/listinstrumentport/listInstrumentPort.go`

Key changes:

- `New([])` now returns `NewEmpty()` instead of constructing a nil pointer allocation.

Why:

- Prevent nil-handle propagation when serializing empty `Ports` payloads.

## Repo: falcon-instrument-hub

Current status in local repo:

- Repo is currently clean (`git status` had no pending local modifications).
- Runtime behavior consumed by tests is coming from the overlay-port-built binary in vcpkg.

Notes:

- During debugging, temporary instrumentation and startup-order experiments were tried and then reverted.
- The persistent, shipped behavior is represented by the rebuilt vcpkg overlay package.

## Runtime protocol/behavior changes validated

- Device config response path now reaches and returns cereal JSON.
- Port payload path moved from plain Go JSON to cereal-compatible `Ports` serialization.
- Prior rapidjson assertion and CGo invalid free crash were eliminated.

## Repo: instrument-script-server

### 1) Static destruction-order fix in ProcessManager singleton

File: `src/server/InstrumentWorkerProxy.cpp`

**ISS version:** `v1.1.7` (commit `829f835`)

Key changes:

- `get_process_manager()` previously returned a function-local `static ipc::ProcessManager manager;`.
- Changed to a heap-backed singleton: `static auto *manager = new ipc::ProcessManager();`.

Why:

- The `InstrumentRegistry` teardown can still call into `ProcessManager` during global static destruction.
- A function-local static can be destroyed too early, leading to use-after-destruction in stop/shutdown paths.
- This was the root cause of the intermittent `exit 139` (SIGSEGV) seen when stress-running `instrument-script-server start`.

How reproduced:

- Stress-looped `instrument-script-server start ...` and captured core dump (`core.1258697`).
- `gdb` backtrace localized crash to `instserver::ipc::ProcessManager::is_alive(int) const` during teardown.

## Repo: falcon-instrument-hub

### 1) Removed double-free of C pointers in port request handler

File: `runtime/internal/handlers/port_request_handler.go`

**Hub version:** `v1.0.8`

Key changes:

- Removed `close()` call on C-API objects after forwarding them via NATS.

Why:

- The C pointer lifetime is managed by the wrapper layer (`cmemoryallocation`).
- Explicit `close()` after publishing caused a double-free in combination with the runtime cleanup registered by `NewAllocation`.

### 2) Re-ordered `startInstruments` and `setupHandlers`

File: `runtime/cmd/main.go`

**Hub version:** `v1.0.9`

Key changes:

- `startInstruments()` now runs **after** handlers are subscribed (not before).

Why:

- Previously, instruments were started before the port request handler was active.
- Hub could miss instrument initialization events, producing empty or stale port state.

### 3) NATS/RPC bug fix

**Hub version:** `v1.0.10` / `v1.0.11`

Key changes:

- Fixed a NATS subject routing / RPC response bug that caused certain hub requests to hang or return stale data.

## Repo: instrument-controller (continued)

### 4) Updated ISS overlay port to `v1.1.7`

File: `ports/instrument-script-server/portfile.cmake` / `vcpkg.json`

Key changes:

- Version pinned to `v1.1.7` (static destruction-order fix).
- Portfile injects local `InstrumentWorkerProxy.cpp` override into the vcpkg build tree:

  ```cmake
  set(ISS_PROXY_SRC_OVERRIDE "${WORKSPACE_ROOT}/instrument-script-server/src/server/InstrumentWorkerProxy.cpp")
  if(EXISTS "${ISS_PROXY_SRC_OVERRIDE}")
      file(COPY "${ISS_PROXY_SRC_OVERRIDE}" DESTINATION "${SOURCE_PATH}/src/server")
  endif()
  ```

Why:

- Allows local workspace fix to be consumed by vcpkg without waiting for upstream release propagation.

### 5) Updated hub overlay port to `v1.0.11`

File: `ports/falcon-instrument-hub/portfile.cmake` / `vcpkg.json`

Key changes:

- Version bumped to `v1.0.11`, `port-version: 9`.
- Portfile now also injects local `main.go` override into vcpkg build tree for `startInstruments` ordering fix:

  ```cmake
  set(HUB_MAIN_OVERRIDE "${WORKSPACE_ROOT}/falcon-instrument-hub/runtime/cmd/main.go")
  if(EXISTS "${HUB_MAIN_OVERRIDE}")
      file(COPY "${HUB_MAIN_OVERRIDE}" DESTINATION "${SOURCE_PATH}/runtime/cmd")
  endif()
  ```

### 6) Manual port construction in data retrieval test (temporary workaround)

File: `tests/data-retrieval-1D/data-retrieval.cpp`

Key changes:

- `InstrumentPort` objects (`getter`, `independantKnob`, `clock`) are manually constructed in the test body.
- Ports are **not** fetched from the hub via the `PORT_PAYLOAD` / `subscribe_port_payload` protocol.

Why (temporary):

- The `port_payload` serialization path (hub → NATS → `RuntimeComms::subscribe_port_payload`) is not yet stable end-to-end.
- Bypassing it allows the rest of the measurement pipeline to be validated independently.

**TODO:** `port_payload` must be fixed before this test reflects real runtime behavior.
The full chain is:
`hub port_request_handler → FALCON.PORT_PAYLOAD (NATS) → falcon-comms subscribe_port_payload → InstrumentPort deserialization`

## Current blocker (as of this note)

- With the above fixes applied, the ISS segfault is resolved.
- The remaining issue is that the `port_payload` protocol is bypassed in the test.
- The Gaussian 1D integration test can pass with manually constructed ports, but end-to-end port discovery from the hub is not yet validated.

## Logs and artifacts used during debugging

- `/tmp/falcon_gaussian1d_after_closefix.log`
- `/tmp/falcon_gaussian1d_after_cmas_fix.log`
- `/tmp/falcon_gaussian1d_after_emptylist_fix.log`
- `/tmp/falcon_gaussian1d_final_state.log`
- `/tmp/iss-daemon.log`
- Core dump: `core.1258697` (used for ISS segfault backtrace)

## Suggested follow-up actions

1. Fix `port_payload` end-to-end: ensure hub serializes `InstrumentPort` list as cereal-compatible JSON that `subscribe_port_payload` can deserialize into `Ports`.
2. Once `port_payload` is stable, remove the manual port construction in `data-retrieval.cpp` and replace with a `request_port_payload()` call.
3. Run stress tests on `instrument-script-server start` to confirm no regression after static destruction-order fix.

---

# Hub Runtime Debug Changes (continued — 2026-05-09)

## Measurement pipeline root cause analysis

With the ISS segfault and CGo memory issues resolved, attention shifted to the full measurement pipeline. Analysis confirmed three interlocking root causes that prevent `DataRetrievalTest.Gaussian1D` from completing end-to-end.

### Root cause 1: Hub built without `-tags falcon_core`

**File**: `instrument-controller/ports/falcon-instrument-hub/portfile.cmake`

The current `go build` invocation does **not** pass `-tags falcon_core`:

```cmake
COMMAND "${CMAKE_COMMAND}" -E env "CGO_LDFLAGS=${FALCON_GO_CGO_LDFLAGS}" go build -o "${GO_OUTPUT}" ./cmd/main.go
```

Without this tag, `falcon_core_stub.go` is compiled instead of `falcon_core.go`. The stub's `ExtractGetters` / `ExtractSetters` implementation parses `parsedData["getters"]` — a plain Go JSON key that does not exist in the cereal-format MeasurementRequest published by `falcon-comms` `RoutineComms`. Result: getters list is always empty, no ISS globals can be built.

### Root cause 2: `InterpreterDaemon` is dead code — nobody handles `PROCESS_REQUEST`

`measure_command_handler.go::sendProcessRequest()` publishes to NATS subject `PROCESS_REQUEST`. The `InterpreterDaemon` struct in `runtime/internal/serverinterpreter/interpreter_daemon.go` subscribes to this subject — but `NewInterpreterDaemon` is **never called** from `cmd/main.go` or `setupHandlers`. The NATS message is published into the void; no measurement execution ever begins.

The only subscriber to `PROCESS_REQUEST` that exists in the codebase is the optional `dummyBackend` in `runtime/external/falcon_runtime_harness/main.go`, which is never started in normal operation.

### Root cause 3: Lua measurement script passes wrong arguments and uses unretrievable buffer

**File**: `instrument-controller/tests/lua/measure_get_set.lua` (compiled from `tests/data-retrieval-1D/measurement-scripts/measure_get_set.tl`)

Two bugs:

1. **Wrong API arity**: Generated Lua bindings require `(id, channel, ...)` but script calls `setter.channel` where `id` is expected:
   - `Mock1Source1:setVoltage(setter.channel, voltage)` → should be `Mock1Source1:setVoltage(setter.id, setter.channel, voltage)`
   - Same for `Mock5Meter1:setSampleRate(getter.channel, sampleRate)` and `Mock5Meter1:setBins(getter.channel, numPoints)`

2. **`measureStream` result is in-process only**: `Mock5Meter1:measureStream(getter.channel)` creates a `data_buffer` inside the ISS process. The hub cannot retrieve this buffer via HTTP RPC; only `GET_DATAPOINT` returns inline float values accessible in the `collect_results_json()` / `measure` response.

### Root cause 4: NATS `MeasureResponse` field name mismatch

Hub `api.MeasureResponse` publishes `{"response": "..."}` (field `response`). Controller's `routine_comms.cpp` subscribes to `FALCON.MEASURE_RESPONSE` and reads `json["response"]` — this field name aligns. However the hub's `handleUploadData` path (which is the only path that ever publishes to `FALCON.MEASURE_RESPONSE`) is never reached because `UPLOAD_DATA` is never published: nothing runs the measurement, so the pipeline stalls before that point.

### Root cause 5: No CGo path to build a valid `MeasurementResponse`

Even if ISS were called and returned data, the hub has no code to construct a cereal-compatible falcon-core `MeasurementResponse`. The `InterpreterDaemon`'s `buildResponseJSON` produces simplified Go JSON, not the cereal format that `hub.cpp::from_json_string<MeasurementResponse>` expects on the controller side.

The CGo path in `falcon_core.go` provides `NewFalconMeasurementRequestFromJSON` and `ExtractGetters/Setters`, but does not yet have a `BuildMeasurementResponse` function that uses:
- `farraydouble.FromData`
- `acquisitioncontext.New(connection, instrumentType, units)`
- `labelledmeasuredarray.FromFArray`
- `measurementresponse.New`

## Repo: falcon-instrument-hub

### `measure_command_handler.go` — NATS subject alignment (port-version 10/11)

**File**: `runtime/internal/handlers/measure_command_handler.go`

`MeasureCommandSubject` changed to `"INSTRUMENTHUB.MEASURE_COMMAND"` and `MeasureResponseSubject` to `"FALCON.MEASURE_RESPONSE"`. These now exactly match `make_measure_command_subject()` and `make_measure_response_subject()` in `falcon/comms/src/routine_comms.cpp`.

## Repo: instrument-controller

### 6) Portfile injects `measure_command_handler.go` override

**File**: `ports/falcon-instrument-hub/portfile.cmake`

Added injection alongside `main.go`:

```cmake
set(HUB_HANDLER_OVERRIDE "${WORKSPACE_ROOT}/falcon-instrument-hub/runtime/internal/handlers/measure_command_handler.go")
if(EXISTS "${HUB_HANDLER_OVERRIDE}")
    file(COPY "${HUB_HANDLER_OVERRIDE}" DESTINATION "${SOURCE_PATH}/runtime/internal/handlers")
endif()
```

## Known issue: `commands_definitions.hpp` `int timestamp` type

The `MeasureCommand` struct in `falcon/comms/include/falcon-comms/commands_definitions.hpp` declares `int timestamp`. In `routine_comms.cpp`, `req.timestamp = time` where `time` is `long long`. This silently truncates. Previously fixed in `routine_comms.cpp` and its header, but the generated `commands_definitions.hpp` was also patched separately. The portfile for `falcon-comms` injects the fixed `.cpp` and `.hpp` into the vcpkg build.

## Current state summary (2026-05-09)

- vcpkg overlay build compiles cleanly.
- ISS starts and accepts HTTP RPC.
- Hub starts and connects to embedded NATS.
- Hub subscribes correctly on `INSTRUMENTHUB.MEASURE_COMMAND` and `FALCON.MEASURE_RESPONSE`.
- Port payload / device config paths work end-to-end.
- **Measurement pipeline is still broken**: `sendProcessRequest` publishes to an unhandled NATS subject; no ISS `measure` call is made; no `FALCON.MEASURE_RESPONSE` is ever published; test times out.
- `data-retrieval.cpp` test bypasses `port_payload` discovery with manually constructed `InstrumentPort` objects.

## Required next steps

1. Add `-tags falcon_core` to hub portfile `go build` invocation; set `PKG_CONFIG_PATH` (already present).
2. Fix `measure_get_set.tl`/`.lua` to pass `getter.id, getter.channel` and loop `getDatapoint` instead of calling `measureStream`.
3. Add `BuildMeasurementResponse` function to `falcon_core.go` using the CGo path.
4. Replace `sendProcessRequest` in `measure_command_handler.go` with a direct ISS `measure` HTTP call, parsing `GET_DATAPOINT` floats from the results and publishing a proper `MeasurementResponse` JSON to `FALCON.MEASURE_RESPONSE`.
5. Bump `port-version` to force vcpkg rebuild.
6. Remove manual port construction bypass in `data-retrieval.cpp` once `port_payload` path is validated.

## Logs and artifacts (continued)

- `/tmp/iss-daemon.log` — ISS startup + instrument load
- `/tmp/falcon_gaussian1d_final_state.log` — last known test run output
