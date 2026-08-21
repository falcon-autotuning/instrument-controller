# Hub gRPC Refactor Change Log and Test Notes

Date: 2026-07-15

This note records the local files changed while adapting the controller/hub
stack to the ISS gRPC/protobuf refactor, plus the current `make test` result
from `tests/hub/log/make_test_out.txt`.

## Repository State Summary

### `instrument-controller`

Modified or added files currently visible in `git status`:

- `ports/falcon-instrument-hub/portfile.cmake`
- `ports/falcon-instrument-hub/vcpkg.json`
- `ports/instrument-data/portfile.cmake`
- `ports/instrument-data/vcpkg.json`
- `ports/instrument-plugin-api/portfile.cmake`
- `ports/instrument-plugin-api/vcpkg.json`
- `ports/instrument-script-server/portfile.cmake`
- `ports/instrument-script-server/vcpkg.json`
- `ports/isa-test-utils/portfile.cmake`
- `ports/isa-test-utils/vcpkg.json`
- `ports/visa-plugin/portfile.cmake`
- `ports/visa-plugin/vcpkg.json`
- `tests/instrument-plugins/mock-multimeter.c`
- `tests/instrument-plugins/mock-voltage-source.c`
- `docs/ISS_GRPC_PROTOBUF_REFACTOR_OBSERVATIONS.md`
- `docs/HUB_GRPC_REFACTOR_CHANGELOG_AND_TEST_NOTES.md`
- `ports/instrument-call-stack/`

Generated or diagnostic files also changed/created:

- `tests/hub/log/make_test_out.txt`
- `tests/test-runs/`

### `instrument-script-server`

Modified files currently visible in `git status`:

- `CMakeLists.txt`
- `cmake/instrument-script-server-config.cmake.in`
- `src/instrument_server_main.cpp`
- `src/worker_main.cpp`
- `cmake/bootstrap` is a dirty submodule/worktree entry.

### `falcon-instrument-hub`

The working tree is currently clean according to `git status --short`.

## Edits Made

### Controller vcpkg overlay ports

- Updated `instrument-script-server` overlay port to package ISS `2.0.0`.
- Bumped `instrument-script-server` port revisions as build/config issues were
  fixed; current local port version is `2.0.0#17`.
- Added ISS gRPC-era dependencies:
  - `grpc`
  - `protobuf`
  - `cxxopts`
  - `instrument-log`
  - `instrument-call-stack[lua]`
  - `instrument-plugin-api[host]`
- Changed `ports/instrument-script-server/portfile.cmake` to use the local
  workspace checkout of `instrument-script-server` when present.
- Added cleanup in the ISS portfile for debug include/share directories and
  generated doc asset directories.
- Added a new overlay port for `instrument-call-stack` with optional `lua`
  feature support.
- Updated `instrument-plugin-api` to `2.0.2` and added compatibility aliases in
  the installed CMake config:
  - `instrument-plugin-api::plugin`
  - `instrument-plugin-api::instrument-plugin-api`
  - `instrument-plugin-api::host`
- Updated `instrument-data` to `1.1.8`.
- Updated `isa-test-utils` to `1.2.4`.
- Updated `visa-plugin` to `1.1.0`.
- Updated `falcon-instrument-hub` port metadata/hash for the local expected hub
  release.

### ISS CMake/package fixes

- Replaced `find_package(grpc CONFIG REQUIRED)` with
  `find_package(gRPC CONFIG REQUIRED)` in ISS CMake.
- Replaced `find_dependency(grpc CONFIG REQUIRED)` with
  `find_dependency(gRPC CONFIG REQUIRED)` in the installed ISS package config.
- Removed the CLI executable from the exported
  `instrument-script-server-targets` package export while still installing the
  executable. This fixed the vcpkg-generated target expecting:

  ```text
  tools/instrument-script-server/instrument-script-server
  ```

  while the installed binary actually lived under:

  ```text
  bin/instrument-script-server
  ```

### ISS source fixes

- Removed invalid `constexpr` usage from string-based log-level parsing.
- Added missing `<vector>` include in `src/instrument_server_main.cpp`.
- Replaced a fragile ranges-based `args_sv()` implementation with an explicit
  loop returning `std::vector<std::string_view>`.
- Fixed string concatenation that attempted to append an integer channel value
  directly to a string; now uses `std::to_string(cmd.channel())`.

### Controller mock plugin updates

Both mock test plugins were updated for `instrument-plugin-api` v2:

- `tests/instrument-plugins/mock-multimeter.c`
- `tests/instrument-plugins/mock-voltage-source.c`

Specific changes:

- Include `plugin-api.h` instead of using only the raw `instrument-plugin.h`.
- Stop accessing opaque `PluginCommand`, `ParamStorage`, and `PluginResponse`
  internals directly.
- Replace `cmd->param_count` with `param_storage_count(cmd->params)`.
- Replace `cmd->params[i]` direct access with `param_storage_get()`.
- Replace old response field writes such as `resp->success`,
  `resp->error_code`, `resp->text_response`, and `resp->return_value` with
  `plugin_response_push()`.
- Replace removed type constants:
  - `PARAM_TYPE_INT32` -> `PARAM_TYPE_INT64`
  - `PARAM_TYPE_FLOAT` -> `PARAM_TYPE_DOUBLE`
- Update plugin entry points to the v2 ABI return type `uint8_t`.
- Use small positive plugin error codes instead of negative values, since the
  v2 ABI returns `uint8_t`.
- For stream responses, push a `PARAM_TYPE_BUFFER` variable containing the
  instrument-data buffer id.
- Syntax-only checks passed for both C files:

  ```text
  clang -fsyntax-only -DINSTRUMENT_PLUGIN_BUILD \
    -Iinstrument-controller/vcpkg_installed/x64-linux-dynamic/include \
    instrument-controller/tests/instrument-plugins/mock-voltage-source.c

  clang -fsyntax-only -DINSTRUMENT_PLUGIN_BUILD \
    -Iinstrument-controller/vcpkg_installed/x64-linux-dynamic/include \
    instrument-controller/tests/instrument-plugins/mock-multimeter.c
  ```

  Remaining warnings are the pre-existing clang extension warnings for constant
  expressions used as array bounds.

## Current `make test` Result

Current log:

```text
tests/hub/log/make_test_out.txt
```

Summary:

```text
0% tests passed, 20 tests failed out of 20
```

Every `DataRetrievalTest.*` case failed with the same outer test symptom:

```text
C++ exception with description "Timeout waiting for MeasureResponse"
```

This is worse than the previous expected/baseline behavior where the run was
understood to pass 17/20 tests and fail 3/20.

## Current Failure Pattern

The current failures do not look like mock plugin compile failures anymore.
The build reaches test execution, and the hub starts enough to answer status,
device-config, and port requests.

The common failure is that measurement requests are accepted by the hub but no
`FALCON.MEASURE_RESPONSE` arrives before the test timeout.

The strongest shared clue is in each current per-test ISS log. Example:

```text
tests/test-runs/DataRetrievalTest_SetVoltage_3271788_1784084938998148534/hub/instrument_server.log
```

contains:

```text
[DAEMON] [MAIN] Starting daemon process
[DAEMON] [START] Another server instance is already running (PID: 127586)
[DAEMON] [START] Failed to start daemon
```

The same stale PID appears across the current run. On inspection after the run,
`ps -p 127586` did not find a live process, while the ISS runtime directory
still contained:

```text
/run/user/84030/instrument-script-server/server.pid
/run/user/84030/instrument-script-server/shutdown.pipe
```

with `server.pid` containing:

```text
127586
```

Current run directories also lack `worker_Meter1.log` and `worker_Source1.log`,
while older July 13 baseline run directories contain those worker logs and show
successful worker command responses. That points to daemon startup/runtime-state
blocking before instrument workers start.

## Additional Log Issues

Some multi-voltage tests also show this hub-side error:

```text
measurement dispatch failed: measure script set_many_voltages:
global "setVoltages": unsupported value type map[string]float64
```

and:

```text
measurement dispatch failed: measure script ramp:
global "setVoltages": unsupported value type map[string]float64
```

Those look like a separate result/value-marshalling issue in the hub measurement
path. They should be investigated after the daemon startup/state problem is
cleared, because the daemon failure currently makes all tests timeout.

## Likely Next Step

Before the next test run, clear stale ISS runtime state or make the hub/ISS
startup path handle it robustly.

Manual cleanup candidate:

```bash
rm -f /run/user/84030/instrument-script-server/server.pid \
      /run/user/84030/instrument-script-server/shutdown.pipe
```

Then rerun:

```bash
make test 2>&1 | tee tests/hub/log/make_test_out.txt
```

Code-side follow-up candidates:

- In ISS, remove stale `server.pid` when `is_already_running()` detects that
  the PID is not alive.
- In the hub, do not treat `cmd.Start()` of `instrument-script-server daemon
  start` as success by itself. Wait briefly and verify daemon readiness over
  gRPC before starting handlers/instruments.
- In the test harness, set `INSTRUMENT_SCRIPT_SERVER_RUNTIME_DIR` to a
  per-test directory so one failed run cannot poison every subsequent test.

## Post PID Cleanup Log Update

After killing the stale ISS PID and rerunning `make test`, the stale daemon
startup problem appears cleared. The current run still reports:

```text
0% tests passed, 20 tests failed out of 20
```

but the failure mode has moved. In the Gaussian get/set run:

```text
tests/test-runs/DataRetrievalTest_Gaussian1DMeasureGetSet_3641090_1784148545096243814
```

the ISS daemon now starts and listens successfully:

```text
[RPC] [START] gRPC Server listening on 127.0.0.1:5555
[DAEMON] [START] Server daemon started (PID: 3641416)
```

The plugin load also gets farther than before:

```text
[PLUGIN_REGISTRY] [LOAD] Loaded plugin: Mock Multimeter ... for protocol: MockMultimeter
[PROCESS] [SPAWN] Worker spawned successfully: PID=3641481
```

The new blocker is API validation in the worker:

```text
[Meter1] [PROXY] Invalid api '.../generated-multimeter-api.yml': Unknown type
[REGISTRY] [CREATE] Failed to start worker for:  Meter1
```

`worker_Meter1.log` confirms the same root error:

```text
[Meter1] [WORKER_MAIN] Invalid api '.../generated-multimeter-api.yml': Unknown type
```

The hub runtime log then shows the measurement dispatch reaching ISS, but the
job fails and no measure response is published:

```text
RunMeasurement returned: resultCount=0 err=measure script measure_get_set:
ISS measure job 1 ended with status JOB_STATUS_FAILED
```

The outer test symptom remains:

```text
C++ exception with description "Timeout waiting for MeasureResponse"
```

The immediate next investigation should be the ISS/instrument-plugin-api schema
type compatibility for generated instrument APIs. The current generated
multimeter API still uses legacy type names such as `int`, `float`, and
`array`; the refactored ISS worker appears not to recognize at least one of
those names. Until the worker accepts the API, `startInstruments` fails, the
instrument registry contains no `Meter1`, and hub measurement commands will
continue ending as `JOB_STATUS_FAILED`.

The previously noted hub-side marshalling issue remains separate:

```text
global "setVoltages": unsupported value type map[string]float64
```

That should be handled after the worker/API type mismatch is resolved, because
the instrument creation failure is currently broad enough to explain the 20/20
measurement timeouts.

## API Type Compatibility Fix

Implemented in ISS:

```text
instrument-script-server/include/instrument-script-server/core/ParsingTools.hpp
instrument-script-server/tests/unit/test_parsing_tools.cpp
instrument-script-server/tests/CMakeLists.txt
```

`mapType()` now accepts both the refactored runtime names and the legacy
instrument API schema names:

```text
integer, int     -> PARAM_TYPE_INT64
boolean, bool    -> PARAM_TYPE_BOOL
array, buffer    -> PARAM_TYPE_BUFFER
float            -> PARAM_TYPE_DOUBLE
string           -> PARAM_TYPE_STRING
```

This should let the worker accept controller-generated API files that still use
`type: int`, `type: float`, and `type: array`, including
`generated-multimeter-api.yml`.

## Cached ISS Package Follow-Up

The first `make test` after the API type compatibility fix still failed 20/20.
The fresh logs showed the same worker error:

```text
[Meter1] [WORKER_MAIN] Invalid api '.../generated-multimeter-api.yml': Unknown type
```

The installed vcpkg header confirmed the controller was still using the old ISS
package:

```text
vcpkg_installed/x64-linux-dynamic/include/instrument-script-server/core/ParsingTools.hpp
```

still only accepted `integer`, not `int`. The `make_test_out.txt` vcpkg section
also showed `instrument-script-server:x64-linux-dynamic@2.0.0#17` reinstalling
in milliseconds instead of rebuilding from the local ISS source.

To force vcpkg to rebuild the overlay package with the local source changes,
the controller ISS overlay port revision was bumped:

```text
instrument-controller/ports/instrument-script-server/vcpkg.json
port-version: 17 -> 18
```

After the next vcpkg install/build, verify that the installed header contains:

```text
type == "integer" || type == "int"
```

before rerunning the full test suite.

## Channel Group IO Resolution Fix

After rebuilding ISS locally as `instrument-script-server@2.0.0#19`, the
installed header did contain the type alias fix, so vcpkg was using the local
ISS source. The next `make test` still failed 20/20, but the worker error moved
again:

```text
[Meter1] [WORKER_MAIN] Invalid api '.../generated-multimeter-api.yml':
Unknown IO in parameters: sample_rate
```

This showed that `load_api()` accepted `type: int`, but only resolved
parameters and outputs against top-level `io:` entries. Controller-generated
APIs use channel-group-local names from `channel_groups[*].io_types`, for
example:

```text
parameters:
  - io: sample_rate
outputs: [stream]
```

Implemented in ISS:

```text
instrument-script-server/src/core/ParsingTools.cpp
instrument-script-server/tests/unit/test_parsing_tools.cpp
```

`load_api()` now builds a scoped lookup for each command. When a command has a
`channel_group`, the scoped lookup combines top-level `io:` definitions with
that channel group's `io_types`, accepting either `name` or `suffix` as the
local IO key. This should resolve names such as `sample_rate`, `bins`, and
`stream` in `generated-multimeter-api.yml`.

The controller ISS overlay port was bumped again so vcpkg rebuilds this new
ISS source:

```text
instrument-controller/ports/instrument-script-server/vcpkg.json
port-version: 19 -> 20
```

## Local Hub Overlay Follow-Up

After the channel-group parser fix, `make test` was rerun and then killed after
early failures. The installed ISS package had advanced to
`instrument-script-server@2.0.0#21`, and the installed core library contained
the channel-group parser strings, so the ISS parser fix was present.

The remaining first-test failure was still:

```text
failed to start instrument from .../multimeter-config.yml via ISS RPC:
rpc error: code = Internal desc = failed to create instrument
```

The fresh test-run directories did not contain `instrument_server.log` or
`worker_Meter1.log`. Reproducing the worker startup directly with the installed
ISS binary showed the multimeter plugin now reaches plugin initialization and
then fails:

```text
[Meter1] [WORKER_MAIN] Loaded plugin: Mock Multimeter v1.0.0 (MockMultimeter)
[PLUGIN] [INIT] Plugin initialization failed with code: 1
[Meter1] [WORKER_MAIN] Plugin initialization failed: -255
```

The mock multimeter returns initialization error `1` when
`MOCK_MULTIMETER_DATA_FILE` is missing or invalid. The C++ integration test does
set that variable before starting the hub, so the likely issue is that the
installed hub binary is still the GitHub/cached `falcon-instrument-hub`
package rather than the local hub source that preserves `os.Environ()` when
starting ISS.

The controller hub overlay port has been updated to use the local
`falcon-instrument-hub` checkout whenever it exists:

```text
instrument-controller/ports/falcon-instrument-hub/portfile.cmake
instrument-controller/ports/falcon-instrument-hub/vcpkg.json
port-version: 56 -> 57
```

The local hub source was also updated so ISS daemon stdout/stderr is captured
under the active test run directory:

```text
falcon-instrument-hub/runtime/cmd/main.go
log/iss-daemon.log
```

Before the next test run, clear any stale ISS runtime PID left by the killed
run, then rebuild vcpkg packages so the local hub is installed.

## Local Hub Rebuild Follow-Up

After the local hub overlay was picked up, the failures moved forward again.
The `DataRetrievalTest.SetVoltage` run produced an ISS daemon log in the active
test-run directory:

```text
tests/test-runs/DataRetrievalTest_SetVoltage_3852969_1784151512567719795/hub/instrument_server.log
```

Both mock instruments were created successfully, but the ISS measure job failed
while loading the generated Lua:

```text
[SERVER] [MEASURE] Missing main(ctx, ...) function.
```

The controller-generated test Lua still uses the older Teal module style:

```text
return { main = Set_Voltage }
```

while the refactored ISS loader expects a global:

```text
function main(ctx, ...)
```

ISS was updated to accept both forms. After `safe_script_file()`, it still
prefers a global `main`, but falls back to `load_result[0].main` when the script
returns a table. A regression test was added for the returned-module shape:

```text
instrument-script-server/src/daemon/CommandHandlers.cpp
instrument-script-server/tests/integration/test_main_function_integration.cpp
```

The same run also showed a separate hub-side marshal failure for
`set_many_voltages` and `ramp`:

```text
global "setVoltages": unsupported value type map[string]float64
```

The hub gRPC client already handled `map[string]interface{}`, but Go does not
treat typed maps such as `map[string]float64` as that same concrete type. The
client now converts typed scalar maps into ISS protobuf mixed maps, with a
focused unit test covering the `setVoltages` shape:

```text
falcon-instrument-hub/runtime/internal/serverinterpreter/client.go
falcon-instrument-hub/runtime/internal/serverinterpreter/client_test.go
```

Overlay ports were bumped again so vcpkg rebuilds the updated local packages:

```text
instrument-controller/ports/instrument-script-server/vcpkg.json
port-version: 21 -> 22

instrument-controller/ports/falcon-instrument-hub/vcpkg.json
port-version: 57 -> 58
```

Verification run locally:

```text
env GOCACHE=/tmp/falcon-hub-go-build go test ./internal/serverinterpreter -run 'TestBuildMeasureJobRequest'
```

The full `go test ./internal/serverinterpreter` package was not usable inside
the Codex sandbox because live startup tests try to bind a TCP socket and fail
with `socket: operation not permitted`.

## Stale Daemon Detection Follow-Up

The next `make test` rebuilt the intended local packages:

```text
falcon-instrument-hub:x64-linux-dynamic@1.0.22#58
instrument-script-server:x64-linux-dynamic@2.0.0#22
```

The previous hub-side `map[string]float64` marshal error no longer appeared,
but every test still failed with `ISS measure job ... JOB_STATUS_FAILED`.
The per-test `iss-daemon.log` files only contained:

```text
Daemon is already running on port 5555
```

and no fresh `instrument_server.log` or worker logs were written under the
test-run directories. This means the suite was talking to a pre-existing ISS
daemon instead of the daemon started for the current test run. The hub then
continued as though startup succeeded, which hid the stale-daemon condition
behind many measurement timeouts.

The local hub startup code was updated to run `instrument-script-server daemon
start` to completion, append the CLI output to the active
`log/iss-daemon.log`, and reject startup when the output says the daemon is
already running. Auto-start failure is now fatal unless the hub is explicitly
started with `--no-iss`, so stale daemon state should fail early instead of
continuing into measurement timeouts:

```text
falcon-instrument-hub/runtime/cmd/main.go
```

The controller hub overlay was bumped again:

```text
instrument-controller/ports/falcon-instrument-hub/vcpkg.json
port-version: 58 -> 59
```

With this change, the next test run should either start a clean daemon and
produce fresh ISS logs, or fail early with the stale daemon message instead of
continuing into misleading measurement failures.

## ISS Stop/Start Race Follow-Up

The next `make test` installed the local hub as:

```text
falcon-instrument-hub:x64-linux-dynamic@1.0.22#59
```

All tests failed early with the intended message:

```text
could not start instrument-script-server: instrument-script-server refused fresh startup: Daemon is already running on port 5555
```

That confirmed the previous failures were caused by an occupied ISS port rather
than stale hub code. The remaining issue may be a stop/start race: the hub sends
`StopDaemon()` and previously attempted to start again after only 200 ms. If ISS
accepts the stop but needs longer to release port `5555`, the subsequent start
correctly refuses to run.

The hub now waits up to 5 seconds for the ISS RPC port to close after requesting
daemon stop before attempting a fresh start. It also centralizes the ISS port
selection so start, stop, and instrument startup use the same value:

```text
falcon-instrument-hub/runtime/cmd/main.go
```

The controller hub overlay was bumped again:

```text
instrument-controller/ports/falcon-instrument-hub/vcpkg.json
port-version: 59 -> 60
```

If the next run still fails with `did not release port 5555 after stop`, the
daemon is not merely slow to shut down and should be stopped or killed outside
the test process before continuing.

## ISS Still Occupies Port After Stop

The next `make test` installed the local hub as:

```text
falcon-instrument-hub:x64-linux-dynamic@1.0.22#60
```

The suite still failed before NATS became available, but the message changed to
the new explicit port-release guard:

```text
could not start instrument-script-server: instrument-script-server did not release port 5555 after stop
```

This means the hub did request daemon shutdown and waited for the configured
5-second port-close window, but something was still listening on
`127.0.0.1:5555`. The remaining blocker is no longer a hidden hub startup
success; ISS is still alive or otherwise holding the port after the stop path.
Before continuing with measurement-level failures, the process occupying port
`5555` needs to be identified and stopped outside the test run.

## Readiness Boundary Follow-Up

After clearing the stale daemon and rerunning to `make_test_out2.txt`, the suite
advanced to measurement execution. Four tests passed:

```text
DataRetrievalTest.SetVoltage
DataRetrievalTest.SetSampleRate
DataRetrievalTest.SetSlope
DataRetrievalTest.GetVoltage
```

The remaining failures showed ISS jobs being submitted while instrument startup
was still in progress. For example, `SetNumberOfSamples` received a measurement
command after `Meter1` started, but before `Source1` had finished starting. The
hub published `STATUS.instrument-server` as soon as NATS handlers were
subscribed, and the C++ tests treat that status as the setup-complete signal.

The hub readiness boundary was updated so operational handlers subscribe first,
ISS instruments are started second, and the status publisher starts last. This
makes `STATUS.instrument-server` mean both handlers and ISS instruments are
ready:

```text
falcon-instrument-hub/runtime/cmd/main.go
falcon-instrument-hub/runtime/internal/handlers/manager.go
```

Instrument startup failure is now fatal instead of a warning, because publishing
ready status without instruments causes misleading measurement timeouts.

The controller hub overlay was bumped again:

```text
instrument-controller/ports/falcon-instrument-hub/vcpkg.json
port-version: 60 -> 61
```

## Mixed Array Protobuf Conversion Follow-Up

After the readiness fix, the next run in `make_test_out2.txt` improved to
11/20 passing. The remaining failed tests were:

```text
DataRetrievalTest.SetManyVoltages
DataRetrievalTest.Ramp
DataRetrievalTest.GetManyVoltages
DataRetrievalTest.GetAllVoltages
DataRetrievalTest.MeasureCurrent
DataRetrievalTest.MeasureIllumination
DataRetrievalTest.Gaussian1DMeasureGetSet
DataRetrievalTest.VoltageSweepCurrent
DataRetrievalTest.VoltageSweepCurrent2D
```

These failures all share the same surface symptom: the hub receives
`JOB_STATUS_FAILED` from ISS and then the C++ test times out waiting for
`FALCON.MEASURE_RESPONSE`. The per-test worker logs show no instrument commands
being executed, so the failures happen inside ISS before Lua reaches the plugin
API.

The likely root cause is the protobuf-to-Lua conversion for `VariableValue`.
`MMap` values were recursively converted into Lua tables, but `MArray` values
were copied directly into Lua. That means globals such as `setters`, `getters`,
`bufferedGetters`, and `bufferedSetters` arrived as arrays of protobuf
`VariableValue` objects rather than normal Lua tables/call stacks. The passing
tests mostly avoid these mixed-array parameters; the failing tests rely on them.

ISS was updated to recursively convert mixed-array items and to fix a misleading
warning that skipped the first manifest parameter when checking for unused
globals:

```text
instrument-script-server/src/daemon/CommandHandlers.cpp
instrument-script-server/tests/unit/test_command_handlers.cpp
instrument-script-server/tests/CMakeLists.txt
```

The controller ISS overlay was bumped so vcpkg rebuilds the local ISS package:

```text
instrument-controller/ports/instrument-script-server/vcpkg.json
port-version: 22 -> 23
```

There is still a diagnostic gap: the hub stops polling when job status becomes
`JOB_STATUS_FAILED`, so it loses the detailed `MeasureJobResult` error and the
test only sees a timeout. If more failures remain after this conversion fix, the
next useful improvement is to return or log failed ISS job result details before
the hub exits the measurement path.
