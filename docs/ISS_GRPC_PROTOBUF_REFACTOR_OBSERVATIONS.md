# ISS gRPC/Protobuf Refactor Observations

Snapshot date: 2026-07-13

This note records the pre-refactor state before pulling the `instrument-script-server`
changes that move communication away from the current RPC/NATS scheme toward
gRPC and protobuf. The main acceptance anchor from the current state is that
`instrument-controller` hub tests pass 17 of 20 tests, with the same three
data-retrieval tests still failing.

## Repository Snapshot

The workspace has three separate repos involved in this integration:

- `instrument-script-server`
  - Branch: `main`
  - HEAD observed: `e446f29`
  - Local status observed: modified `cmake/bootstrap` submodule
  - Current shape: CMake/C++ daemon plus worker processes, installable
    `instrument-script-server`, `instrument-worker`, and
    `libinstrument-script-server-core.so`.
- `falcon-instrument-hub`
  - Branch: `main`
  - HEAD observed: `06ecc2a`
  - Local status observed: clean
  - Current shape: Go runtime binary under `runtime/`, built as
    `runtime/bin/instrument-hub`, with NATS handlers and an ISS client bridge.
- `instrument-controller`
  - Branch: `main`
  - HEAD observed: `6662021`
  - Local status observed: untracked `tests/test-runs/`
  - Current shape: C++ integration tests that build mock instrument plugins,
    generate Teal/Lua artifacts, launch `instrument-hub`, and communicate with
    the hub through `falcon-comms`.

The controller currently consumes both ISS and hub through overlay vcpkg ports:

- `instrument-controller/ports/instrument-script-server/vcpkg.json`
  currently pins `instrument-script-server` at `1.1.11`, port version `14`.
- `instrument-controller/ports/falcon-instrument-hub/vcpkg.json`
  currently pins `falcon-instrument-hub` at `1.0.21`, port version `55`.

## Current ISS Structure

The pre-refactor ISS repo is organized around these important surfaces:

- Public headers: `instrument-script-server/include/instrument-script-server/`
- Daemon/server implementation: `instrument-script-server/src/server/`
- Worker IPC implementation: `instrument-script-server/src/ipc/`
- Generic worker executable: `instrument-script-server/src/workers/`
- Plugin loading and registry: `instrument-script-server/src/plugin/`
- CLI/daemon entry point:
  `instrument-script-server/src/server/instrument_server_main.cpp`
- Documentation anchors:
  `instrument-script-server/docs/ARCHITECTURE.md` and
  `instrument-script-server/docs/IPC_PROTOCOL.md`

The current ISS architecture is still multi-process:

- The daemon owns lifecycle, registry, jobs, sync coordination, and RPC serving.
- Each instrument runs as a separate `instrument-worker` process.
- Daemon-to-worker communication uses Boost.Interprocess queues with JSON
  payloads such as `SerializedCommand` and `CommandResponse`.
- Remote command/control is exposed through the current HTTP JSON RPC server,
  represented in code by `HttpRpcServer`, `ServerDaemon::set_rpc_port`, and the
  hub's `serverinterpreter.ScriptServerClient`.

## Current Hub Communication Boundaries

The current `falcon-instrument-hub` is the adapter between controller-side NATS
messages and ISS HTTP RPC.

NATS-facing hub pieces:

- `runtime/internal/networking/nats.go` starts or connects to NATS. In the test
  harness it starts embedded NATS, normally on `127.0.0.1:4222`.
- `runtime/internal/handlers/port_request_handler.go`
  subscribes to `INSTRUMENTHUB.PORT_REQUEST` and publishes `FALCON.PORT_PAYLOAD`.
- `runtime/internal/handlers/measure_command_handler.go`
  subscribes to `INSTRUMENTHUB.MEASURE_COMMAND`, publishes data to
  `FALCON.MEASURE_DATA.<hash>`, and publishes command completion on
  `FALCON.MEASURE_RESPONSE`.
- `runtime/internal/handlers/status_handler.go` publishes status on
  `STATUS.instrument-server`.

ISS-facing hub pieces:

- `runtime/cmd/main.go` starts ISS with `instrument-script-server daemon start`,
  injects `INSTRUMENT_SCRIPT_SERVER_RPC_PORT` from `instrument-server-port`,
  and starts instruments through ISS RPC.
- `runtime/internal/serverinterpreter/client.go` is the current HTTP JSON RPC
  client. It posts to `http://<host>:<port>/rpc`.
- `runtime/internal/serverinterpreter/script_dispatcher.go` calls ISS
  `measure`, then calls ISS `read_buffer` for buffer-valued returns.
- `runtime/internal/serverinterpreter/types.go` models the current RPC request,
  RPC response, and ISS `collect_results_json` entries.

This means the existing flow is not simply "controller talks directly to ISS".
The controller still talks NATS/falcon-comms to the hub; the hub translates to
ISS RPC and translates results back onto NATS subjects.

## Controller Test Harness

The key integration harness is
`instrument-controller/tests/instrument-control/data-retrieval.cpp`.

Observed setup behavior:

- Builds test data from `tests/test-data/gen_data.cpp`.
- Expands instrument API templates into generated YAML files.
- Generates Teal instrument libraries using `teal-api-gen-cli`.
- Compiles measurement `.tl` files into Lua under `tests/lua/`.
- Sets `INSTRUMENT_SCRIPT_SERVER_OPT_LUA_LIB` for ISS.
- Sets `MOCK_MULTIMETER_DATA_FILE` to Gaussian and linear data files.
- Sets `NATS_URL=nats://localhost:4222`.
- Writes a per-test `test-config.yaml` with:
  - `instrument-server-port: 5555`
  - `start-embedded-nats: true`
  - multimeter and source instrument configs
  - mock plugin paths
  - generated instrument API paths
  - per-test local database and working directories
- Starts `instrument-hub start --hub-config <path> --iss-lib-path <libdir>
  --working-dir <run>/hub --iss-binary <vcpkg bin>/instrument-script-server`.
- Waits for NATS and the hub to be ready before each test body.

The post-refactor hub should preserve this contract unless the controller tests
are intentionally migrated:

- `instrument-hub` remains the process launched by the controller test.
- `instrument-server-port: 5555` remains the configured ISS endpoint, even if
  the implementation behind it becomes gRPC.
- The controller-visible NATS subjects and response semantics remain compatible
  enough for `falcon-comms` to see the same behavior.
- The hub still starts ISS, starts mock instruments, dispatches Lua scripts, and
  returns `MeasureResponse` messages within the controller timeout.

## Pre-Refactor Test Baseline

Reference log:
`instrument-controller/tests/hub/log/make_test_out.txt`

Observed summary:

- `85% tests passed, 3 tests failed out of 20`
- Total test time: `82.51 sec`
- Passing tests: 17 of 20
- Failing tests:
  - `DataRetrievalTest.Gaussian1DMeasureGetSet`
  - `DataRetrievalTest.VoltageSweepCurrent`
  - `DataRetrievalTest.VoltageSweepCurrent2D`

The three failures all time out waiting for `MeasureResponse`. The log shows
the hub/ISS startup sequence succeeds, instruments start, NATS handlers
subscribe, the measure command handler receives commands, and status continues
publishing. The failing behavior is specifically that the controller does not
receive the expected measurement response before the test timeout.

The first 17 tests exercise the existing port request and scalar get/set path:

- `SetVoltage`
- `SetSampleRate`
- `SetNumberOfSamples`
- `SetManyVoltages`
- `Ramp`
- `SetSlope`
- `SetTriggerLeader`
- `GetVoltage`
- `GetSampleRate`
- `GetNumberOfSamples`
- `GetSlope`
- `GetTriggerLeader`
- `GetManyVoltages`
- `GetAllVoltages`
- `MeasureCurrent`
- `MeasureIllumination`
- `MeasureLeakage`

Those 17 passing tests are the practical compatibility baseline after the ISS
gRPC/protobuf refactor is reflected in `falcon-instrument-hub`.

## Refactor Watch Points

When pulling the ISS refactor and adapting the hub, watch these seams first:

- Replace `serverinterpreter.ScriptServerClient` HTTP JSON RPC calls with the
  new gRPC/protobuf client while preserving the hub-facing dispatcher interface.
- Re-map current RPC commands: `start`, `stop`, `list`, `measure`, and
  `read_buffer`.
- Preserve buffer result resolution. Current hub logic expects ISS measure
  results with scalar returns or buffer IDs, then reads buffer data before
  building the controller-facing `MeasurementResponse`.
- Preserve NATS controller subjects unless `falcon-comms` is migrated at the
  same time:
  - `INSTRUMENTHUB.PORT_REQUEST`
  - `FALCON.PORT_PAYLOAD`
  - `INSTRUMENTHUB.MEASURE_COMMAND`
  - `FALCON.MEASURE_RESPONSE`
  - `FALCON.MEASURE_DATA.<hash>`
  - `STATUS.instrument-server`
- Preserve startup ordering in the hub: ISS daemon starts first, NATS handlers
  subscribe, then instruments are started so initialization messages are not
  missed.
- Re-check timeout behavior. The controller waits on NATS responses, while the
  hub's ISS dispatcher currently uses a longer internal timeout. If gRPC calls
  block, fail fast, or stream differently, the hub must still publish an
  explicit NATS response for controller-visible completion.
- Update vcpkg port metadata and build dependencies. The controller log already
  includes `protobuf`/`protobuf-c`, but current ISS port dependencies do not
  list gRPC. The refactor may require new vcpkg dependencies and CMake exports.

## Useful Validation Commands

From `instrument-controller`, the relevant validation remains:

```sh
make test
```

For focused debugging, the CTest names from the baseline are the important
targets. The expected near-term post-refactor outcome is still 17 passing tests
out of 20, with the same three known measurement-response timeouts unless that
path is fixed as part of the refactor.

## ISS Update Comparison

After the ISS update, the observed `instrument-script-server` HEAD changed from
`e446f29` to `f97b6dc`. The local submodule status still shows
`cmake/bootstrap` as modified.

Major structural changes compared with the prior ISS code base:

- The project version moved to `2.0.0`.
- HTTP JSON RPC has been replaced by a protobuf/gRPC daemon API:
  - old: `include/instrument-script-server/server/HttpRpcServer.hpp`
    and `src/server/HttpRpcServer.cpp`
  - new: `include/instrument-script-server/daemon/GrpcServer.hpp`
    and `src/daemon/GrpcServer.cpp`
- A new proto contract exists at
  `instrument-script-server/proto/instserver/daemon/v1/daemon_messages.proto`.
- `proto/CMakeLists.txt` builds and installs `instserver-proto`, including both
  protobuf and gRPC generated C++ sources.
- The old `server/` include and source namespace was reorganized under
  `daemon/`.
- Former plugin internals moved toward a `core/` and external plugin API shape:
  `PluginLoader` is now under `core/`, and ISS depends on
  `instrument-plugin-api` instead of carrying `PluginInterface.h` locally.
- The worker entry point changed from `src/workers/generic_worker_main.cpp` to
  `src/worker_main.cpp`.
- The daemon executable is now explicit:
  `instrument-script-server-daemon` is built from `src/daemon/daemon_main.cpp`.
- The user-facing CLI is now `src/instrument_server_main.cpp`, but CMake only
  builds it when `BUILD_CLI=ON`.
- A C++ gRPC client wrapper was added:
  `include/instrument-script-server/client/instrument-server-client.hpp` and
  `src/client/instrument-server-client.cpp`. It uses native protobuf request
  and response types and a default five second per-RPC deadline.
- Several docs and examples were removed or condensed, including the old
  `docs/ARCHITECTURE.md`, `docs/IPC_PROTOCOL.md`, and `INSTALL.md`.

Important dependency/build differences:

- `instrument-script-server/vcpkg.json` now declares version `2.0.0`.
- New/updated dependencies include `grpc`, `cxxopts`,
  `instrument-data >= 1.1.8`, `instrument-plugin-api >= 2.0.2` with
  `host`/`plugin` features, `instrument-log`, and `instrument-call-stack` with
  `lua`.
- `grpc` is listed twice in the updated ISS `vcpkg.json`; that is harmless for
  understanding the migration, but worth cleaning before publishing a port.
- Current CMake options default to:
  - `BUILD_TESTS=OFF`
  - `BUILD_C_CLIENT=OFF`
  - `BUILD_CLI=OFF`
- Since `BUILD_CLI=OFF` by default, the controller's existing local overlay
  port will not install `instrument-script-server` unless the port explicitly
  passes `-DBUILD_CLI=ON`.

The new gRPC service shape:

- `DaemonStatus`
- `StartInstrument`
- `StopInstrument`
- `InstrumentStatus`
- `ListInstruments`
- `MeasureJob`
- `JobStatus`
- `MeasureJobResult`
- `JobList`
- `CancelJob`
- `Discover`
- `ListDataBuffers`
- `ReleaseBuffer`
- `GetBufferMetadata`
- `StopDaemon`

Important behavioral changes from the hub's point of view:

- There is no longer a generic HTTP request of the form
  `{ "command": "...", "params": ... }`.
- Instrument startup now maps to `StartInstrumentRequest` with fields
  `config_path`, `plugin_path`, and `log_level`.
- Instrument listing now returns repeated `instrument_name`, not a top-level
  JSON field named `instruments`.
- Measurement execution is job based:
  - submit with `MeasureJob`
  - poll with `JobStatus`
  - collect with `MeasureJobResult`
- `MeasureJobResult` returns repeated `CommandResult` values. Each command has
  `instrument_name`, optional `channel`, optional `group`, `verb`,
  `executed_at`, and repeated typed `param` values.
- Return values are now protobuf `VariableValue` oneofs, not the old JSON
  `return.type` / `return.value` / `return.buffer_id` object.
- Buffer-valued returns appear as `TypedParameter` entries with
  `type = LUA_TYPES_DATA_BUFFER`; the buffer id is carried in
  `param.value.s`, with optional metadata in `param.dbmeta`.
- There is no `ReadBuffer` RPC in the proto. The proto explicitly notes that
  buffer reading is handled by the CLI process because it streams data from
  local shared memory to stdout.

One compatibility detail is good news: `INSTRUMENT_SCRIPT_SERVER_RPC_PORT` still
controls the daemon RPC port, and the default remains `8555`. The controller
test config's `instrument-server-port: 5555` can still be honored by the hub.

## How The Hub Should Be Updated

The safest hub migration is to keep the controller-facing NATS contract stable
and replace only the ISS-facing bridge. In other words, `falcon-comms` should
still see the same subjects and `MeasureResponse` behavior while the hub talks
to ISS through gRPC.

Recommended steps:

1. Preserve the existing NATS handlers and subjects:
   - `INSTRUMENTHUB.PORT_REQUEST`
   - `FALCON.PORT_PAYLOAD`
   - `INSTRUMENTHUB.MEASURE_COMMAND`
   - `FALCON.MEASURE_RESPONSE`
   - `FALCON.MEASURE_DATA.<hash>`
   - `STATUS.instrument-server`

2. Replace `runtime/internal/serverinterpreter/client.go` with a gRPC client.
   Generate Go bindings from
   `instrument-script-server/proto/instserver/daemon/v1/daemon_messages.proto`
   and add the Go dependencies for `google.golang.org/grpc` and
   `google.golang.org/protobuf`. Keep the current high-level Go method names
   where possible:
   - `ListInstruments()`
   - `StartInstrument(configPath, pluginPath)`
   - `StopInstrument(name)`
   - `Measure(scriptPath, globals, typeManifest)` or a renamed internal
     `RunMeasureJob(...)`

3. Convert hub globals to protobuf `VariableValue`.
   The hub currently sends `map[string]interface{}` and a JSON-like
   `typeManifest`. The new proto requires:
   - `Globals.map[string]VariableValue`
   - `TypeManifest.parameters[]` with `name` and `LuaTypes`

   Practical mapping:
   - Go `nil` -> `VariableValue.is_nil`
   - integer -> `VariableValue.i`
   - float -> `VariableValue.d`
   - bool -> `VariableValue.b`
   - string -> `VariableValue.s`
   - `[]T` -> the matching typed array when homogeneous
   - `map[string]interface{}` -> `VariableValue.m_map`
   - mixed arrays -> `VariableValue.m_array`

   ISS currently uses the manifest names to decide which globals become
   positional `main(ctx, ...)` arguments. Except for call stacks, it converts
   the actual `VariableValue` to Lua without relying heavily on the declared
   enum, so the hub can set the enum from the value shape.

4. Rebuild the measurement dispatch flow around jobs.
   The replacement for the old synchronous HTTP `measure` call should:
   - call `MeasureJob`
   - poll `JobStatus` until `JOB_STATUS_COMPLETED`, `JOB_STATUS_FAILED`, or
     `JOB_STATUS_CANCELLED`
   - call `MeasureJobResult`
   - translate `CommandResult` back into the existing
     `serverinterpreter.ResolvedCallResult` shape expected by
     `measure_command_handler.go`

5. Adapt result translation carefully.
   The existing `measure_command_handler.go` expects `ResolvedCallResult` with:
   - `Instrument`
   - `Verb`
   - `Return.Type`
   - `Return.Value`
   - optional `Return.BufferID`
   - optional resolved `BufferData`

   With the new proto:
   - use `CommandResult.instrument_name` for `Instrument`
   - use `CommandResult.verb` for `Verb`
   - use each `TypedParameter` as a return value
   - map `LUA_TYPES_DOUBLE`, `INT64`, `BOOL`, and `STRING` to scalar returns
   - map `LUA_TYPES_DATA_BUFFER` to `Return.Type = "buffer"` and
     `Return.BufferID = param.value.s`

6. Decide how buffer data will be read.
   This is the biggest open integration point. The old hub calls HTTP
   `read_buffer` and receives `[]float64`. The new ISS proto only exposes
   buffer metadata/list/release over gRPC; actual buffer reading is in the CLI.

   Short-term options:
   - Keep/install the ISS CLI and have the hub exec
     `instrument-script-server buffer read <buffer_id> --json`, then parse the
     JSON `data` array.
   - Add a `ReadBuffer` gRPC method to ISS so the Go hub can fetch buffer data
     directly.
   - Add a cgo bridge from hub to `instrument-data` and read the shared buffer
     in-process.

   Best long-term option: add a real `ReadBuffer` gRPC RPC or streaming RPC to
   ISS. It keeps hub behavior explicit and avoids shelling out from a long-lived
   service.

7. Update hub startup/stop logic.
   Two workable paths:
   - Compatibility path: keep the `iss-binary` flag pointing at
     `instrument-script-server`, ensure the controller vcpkg port builds and
     installs the CLI, and keep calling `instrument-script-server daemon start`
     / `daemon stop`.
   - Cleaner path: add a separate daemon binary setting, launch
     `instrument-script-server-daemon` directly, and stop it with gRPC
     `StopDaemon`.

   The compatibility path is likely fastest because the current controller test
   already passes `--iss-binary <vcpkg bin>/instrument-script-server`.

8. Update the controller ISS overlay port.
   The current `instrument-controller/ports/instrument-script-server/portfile.cmake`
   calls `vcpkg_cmake_configure` without options. With the updated ISS, it
   should pass at least:

   ```cmake
   OPTIONS
     -DBUILD_CLI=ON
   ```

   `BUILD_CLI=ON` automatically enables `BUILD_C_CLIENT=ON` in the ISS CMake.
   The port metadata should also be updated from `1.1.11#14` to the new ISS
   `2.0.0` dependency set, including gRPC and the updated instrument libraries.

9. Keep startup ordering unchanged.
   The hub should still start ISS, subscribe NATS handlers, then start
   instruments. That ordering was part of the pre-refactor fix for avoiding
   missed initialization messages.

10. Validate in layers:
    - Build/install updated ISS through the controller overlay port.
    - Build hub with generated gRPC bindings.
    - Run hub-side unit tests for `serverinterpreter` conversion functions.
    - Run `instrument-controller` `make test` and compare against the 17/20
      baseline in this document.

Expected first compatibility goal: the same 17 of 20 controller tests still
pass after the ISS 2.0.0 gRPC/protobuf migration is reflected in the hub. The
three pre-existing `MeasureResponse` timeout tests may remain failing unless
the buffered measurement response path is also fixed.
