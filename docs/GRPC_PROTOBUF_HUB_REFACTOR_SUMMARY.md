# gRPC/Protobuf Hub Refactor Summary

Date: 2026-07-22

This note summarizes the changes made across `instrument-script-server`,
`falcon-instrument-hub`, `teal-api-gen`, and `instrument-controller` so the
hub/controller integration works with the ISS gRPC/protobuf refactor.

The current validation anchor is:

```text
instrument-controller/tests/hub/log/make_test_out.txt
```

Current result:

```text
100% tests passed, 0 tests failed out of 20
Total Test time (real) = 72.77 sec
```

The successful run used these controller overlay versions:

```text
falcon-instrument-hub:x64-linux-dynamic@2.0.2#64
instrument-script-server:x64-linux-dynamic@2.0.2#26
teal-api-gen:x64-linux-dynamic@1.0.3#4
```

The log confirms that `instrument-script-server@2.0.2#26` was rebuilt before
the tests:

```text
Building instrument-script-server:x64-linux-dynamic@2.0.2#26...
```

## What gRPC and Protobuf Are

Protocol Buffers, usually called protobuf, are a language-neutral data format
and interface definition system. A `.proto` file defines messages such as
requests, responses, enums, arrays, and service APIs. Code generators then
produce strongly typed C++, Go, Python, or other language bindings from that
schema.

gRPC is a remote procedure call framework that commonly uses protobuf messages
as the request and response payloads. Instead of constructing ad hoc JSON
requests and posting them to an HTTP endpoint, a client calls generated methods
such as `StartInstrument`, `MeasureJob`, or `ReadBuffer`. gRPC serializes the
protobuf messages, sends them over HTTP/2, and deserializes typed responses on
the other side.

For this stack, protobuf provides the shared contract between the Go hub and the
C++ ISS daemon, while gRPC provides the transport and method-call layer.

## High-Level Architecture After the Refactor

The controller-facing architecture did not become "controller talks directly to
ISS". The hub is still the adapter between controller-side NATS traffic and ISS
instrument execution.

Current flow:

```text
instrument-controller tests
        |
        | falcon-comms / NATS
        v
falcon-instrument-hub
        |
        | gRPC + protobuf
        v
instrument-script-server daemon
        |
        | Boost.Interprocess IPC
        v
instrument-worker processes
        |
        | instrument-plugin-api v2
        v
mock or real instrument plugins
```

The external controller contract remains NATS-based. The important NATS
subjects still include:

- `STATUS.instrument-server`
- `INSTRUMENTHUB.PORT_REQUEST`
- `FALCON.PORT_PAYLOAD`
- `INSTRUMENTHUB.MEASURE_COMMAND`
- `FALCON.MEASURE_RESPONSE`
- `FALCON.MEASURE_DATA.<hash>`

The internal hub-to-ISS contract changed from HTTP JSON RPC to gRPC/protobuf.

## Hub Changes

The main hub-side refactor is in:

```text
falcon-instrument-hub/runtime/internal/serverinterpreter/
```

### gRPC Client

`runtime/internal/serverinterpreter/client.go` now implements
`ScriptServerClient` as a gRPC client for the ISS daemon.

The client creates a local insecure gRPC connection to the configured ISS host
and port:

```go
grpc.NewClient(
    fmt.Sprintf("%s:%d", host, port),
    grpc.WithTransportCredentials(insecure.NewCredentials()),
)
```

The hub imports generated protobuf/gRPC bindings from:

```text
runtime/internal/issproto/instserver/daemon/v1/
```

Those generated files are produced from the ISS daemon protobuf definitions and
provide the Go types and client interface used by the hub, including:

- `DaemonServiceClient`
- `ListInstrumentsRequest`
- `StartInstrumentRequest`
- `StopInstrumentRequest`
- `MeasureJobRequest`
- `JobStatusRequest`
- `MeasureJobResultRequest`
- protobuf value types used for Lua globals and type manifests

### Replaced HTTP JSON RPC Calls

The old hub client posted JSON RPC requests to ISS. The refactored client now
calls typed gRPC methods:

- `ListInstruments`
- `StartInstrument`
- `StopInstrument`
- `StopDaemon`
- `MeasureJob`
- `JobStatus`
- `MeasureJobResult`
- `ReadBuffer`

The hub still exposes the same higher-level dispatcher shape to the rest of the
runtime. `ScriptDispatcher.RunMeasurement(...)` continues to accept a script
name, globals, and a type manifest, and returns resolved call results.

Internally, the flow is now:

1. Build a `MeasureJobRequest` protobuf message.
2. Convert hub/controller globals into protobuf `VariableValue` messages.
3. Convert Teal type manifest entries into protobuf `LuaTypes`.
4. Call ISS `MeasureJob`.
5. Poll `JobStatus` until the job completes.
6. Fetch `MeasureJobResult`.
7. Convert protobuf job results back into the hub's `ISSCallResult` shape.
8. For buffer returns, call `ReadBuffer` and inline the numeric data.
9. Let the NATS measure handler build and publish controller-facing responses.

### Startup and Shutdown Behavior

The hub still owns the test/runtime lifecycle:

- Start the ISS daemon process.
- Wait for the gRPC endpoint to become usable.
- Start configured instruments through ISS.
- Start embedded NATS for tests when requested.
- Subscribe the status, port, log, and measurement handlers.
- Stop instruments and the ISS daemon during shutdown.

Readiness is now checked through short gRPC calls such as
`ListInstrumentsWithTimeout(...)`. This matters because "process started" is
not enough; the daemon must also be listening on the gRPC port before the hub
starts using it.

The hub client also closes gRPC connections after lifecycle helper calls, so
long test runs do not accumulate stale client resources.

### Runtime Lua Scripts

Hub-provided Lua scripts were updated to use the refactored ISS call-stack API.
They now construct `instrument_call_stack` userdata instead of relying on the
old string target form.

Example shape:

```lua
local cs = instrument_call_stack.new({
    instrument = params.instrument,
    command = "SET_VOLTAGE",
    channel = params.channel
})
ctx:call(cs, {
    channel = params.channel,
    voltage = params.voltage
})
```

Two things are intentional here:

- `channel = params.channel` in the call stack records the target channel in the
  gRPC/protobuf-era call metadata.
- `channel = params.channel` in the params table satisfies the command's
  channel-group parameter as declared by the generated instrument API.

Affected script families include:

- `set_voltage.lua`
- `get_voltage.lua`
- `measure_current.lua`
- `ramp_voltage.lua`
- `sweep_1d.lua`
- `sweep_2d.lua`
- `dc_get_set.lua`

## `teal-api-gen` Changes

`teal-api-gen@1.0.3#4` generates Lua instrument libraries that match the new ISS
Lua calling convention.

The generated libraries now:

- Create `instrument_call_stack` objects.
- Fill `instrument`, `command`, and channel metadata on the call stack.
- Keep the channel value in the Lua argument table for channel-group commands.
- Preserve non-channel parameters such as `voltage`, `sample_rate`, and `bins`.

Generated controller examples:

```lua
local cs = instrument_call_stack.new({
   instrument = id,
   command = "SET_SAMPLE_RATE",
   channel = channel,
})
return context:call(cs, { channel = channel, sample_rate = sample_rate })
```

```lua
local cs = instrument_call_stack.new({
   instrument = id,
   command = "GET_DATAPOINT",
   channel = channel,
})
return context:call(cs, { channel = channel })
```

This was the correct compatibility point: the Lua must provide both call target
metadata and command parameters. The call stack alone tells ISS where the call
is directed; the parameter table tells the worker/plugin what command arguments
to execute.

## ISS Changes Needed for Compatibility

The ISS refactor introduced the new gRPC/protobuf daemon API, but the
controller/hub tests exposed a few compatibility issues in the Lua-to-worker
path.

### gRPC/Protobuf Packaging

The controller overlay port for ISS now depends on:

- `grpc`
- `protobuf`
- `instrument-call-stack[lua]`
- `instrument-plugin-api[host]`
- `instrument-log`
- `cxxopts`
- existing CMake/Lua/YAML/Boost dependencies

The ISS port is currently:

```text
instrument-script-server 2.0.2#26
```

The portfile can use the local workspace checkout of `instrument-script-server`
when present, otherwise it falls back to the tagged GitHub source.

### Lua `RuntimeContext` Parameter Handling

The final green run required two important fixes in ISS
`RuntimeContext::call(...)`.

First, ISS must not append a duplicate implicit `channel` when the Lua table
already contains one. During debugging this produced worker commands such as:

```text
channel, voltage, channel
```

for `SET_VOLTAGE`, which caused worker parameter-count failures.

Second, table-style Lua parameters must be emitted in the API-declared command
order, not alphabetically. The worker validates command parameter order against
the generated API. A sorted map caused `SET_BINS` to arrive as:

```text
bins, channel
```

while the API expected:

```text
channel, bins
```

The fix was to build the table lookup by name, but emit final parameters by
iterating the parsed command's `parameters` vector in order.

The same logic also supports synthesizing the channel parameter from
`CallStack.channel` if a caller omitted `channel` from the table.

## Controller Changes

Most controller changes were integration and compatibility updates so the
existing C++ test harness could run against the new hub/ISS path.

### Overlay Ports

Updated controller overlay ports include:

- `falcon-instrument-hub` -> `2.0.2#64`
- `instrument-script-server` -> `2.0.2#26`
- `teal-api-gen` -> `1.0.3#4`
- `instrument-plugin-api`
- `instrument-data`
- `isa-test-utils`
- `visa-plugin`

A new overlay port was also added for:

- `instrument-call-stack`

The ISS port adds the new gRPC/protobuf-era dependencies listed above.

The `teal-api-gen` port intentionally downloads the tagged GitHub source rather
than using a local workspace override. That matches the workflow of tagging
`teal-api-gen`, then updating the controller port hash and version.

### Generated Lua and Teal Assets

Controller generated Lua libraries under:

```text
tests/instrument-lua-libs/
```

now use the new call-stack form and pass channel parameters explicitly.

Representative files:

- `tests/instrument-lua-libs/source.lua`
- `tests/instrument-lua-libs/multimeter.lua`

Measurement scripts under:

```text
tests/lua/
tests/instrument-control/measurement-scripts/
```

were kept aligned with the new generated library behavior. The important
runtime result is that set/get/measure scripts now call ISS using typed
`CallStack` userdata and channel-aware parameter tables.

### Mock Plugin Updates

The mock controller plugins were updated for the v2 plugin API and the new
channel parameter naming.

Files:

- `tests/instrument-plugins/mock-multimeter.c`
- `tests/instrument-plugins/mock-voltage-source.c`

Important changes:

- Include `plugin-api.h` for the v2 helper API.
- Stop accessing opaque command/response internals directly.
- Use `param_storage_count(...)` and `param_storage_get(...)`.
- Use `plugin_response_push(...)` for return values.
- Return `uint8_t` plugin status codes.
- Use small positive plugin error codes instead of negative values.
- Accept the refactored channel parameter name `channel`.
- Return output names that match the generated API:
  - multimeter datapoints return `voltage`
  - multimeter streams return `stream`
  - voltage source reads return `voltage`

This was necessary because the generated API's channel group is named `analog`,
but the actual command parameter introduced by the refactor is:

```yaml
channel_parameter:
  name: channel
```

The plugins previously looked for a parameter named `analog`. That caused
commands to reach the worker but fail inside the plugin with error code `2`.

## Debugging Progression

The failures moved through several distinct phases. Each phase revealed a
different boundary that needed to be aligned.

### Phase 1: ISS gRPC Endpoint Not Available

Early runs failed with:

```text
rpc error: code = Unavailable ... dial tcp 127.0.0.1:5555: connect: connection refused
```

This showed that the hub was trying to dispatch measurements before ISS was
ready or after ISS had exited. Startup readiness checks and daemon lifecycle
cleanup were tightened around the gRPC endpoint.

### Phase 2: Duplicate Channel Parameter

After generated Lua started using `CallStack.channel`, ISS also appended an
implicit `channel` param. When Lua also passed `channel` in the table, the
worker saw duplicate channel parameters and rejected the command.

Fix:

- ISS now detects whether the command parameter list already contains
  `channel` before appending one from the call stack.

### Phase 3: Table Parameter Ordering

`SET_BINS` and similar table-style commands failed because ISS rebuilt params
through a sorted map. The worker validates command parameters in API order.

Fix:

- ISS now uses a name lookup for table values but emits params by iterating the
  API command parameter vector.

### Phase 4: Plugin Parameter Name Mismatch

Commands reached workers, but plugins still expected `analog` while the
refactored API sent `channel`.

Fix:

- Mock plugins now use `channel` as the channel parameter name.

### Phase 5: Response Publishing

Once plugin responses were valid, the hub could convert ISS call results into
controller-facing NATS `MeasureResponse` messages. This resolved the remaining
test timeouts in measurement and buffered sweep tests.

## Final Test Result

The final run passed all integration tests:

```text
 1/20 Test  #1: DataRetrievalTest.SetVoltage ................   Passed
 2/20 Test  #2: DataRetrievalTest.SetSampleRate .............   Passed
 3/20 Test  #3: DataRetrievalTest.SetNumberOfSamples ........   Passed
 4/20 Test  #4: DataRetrievalTest.SetManyVoltages ...........   Passed
 5/20 Test  #5: DataRetrievalTest.Ramp ......................   Passed
 6/20 Test  #6: DataRetrievalTest.SetSlope ..................   Passed
 7/20 Test  #7: DataRetrievalTest.SetTriggerLeader ..........   Passed
 8/20 Test  #8: DataRetrievalTest.GetVoltage ................   Passed
 9/20 Test  #9: DataRetrievalTest.GetSampleRate .............   Passed
10/20 Test #10: DataRetrievalTest.GetNumberOfSamples ........   Passed
11/20 Test #11: DataRetrievalTest.GetSlope ..................   Passed
12/20 Test #12: DataRetrievalTest.GetTriggerLeader ..........   Passed
13/20 Test #13: DataRetrievalTest.GetManyVoltages ...........   Passed
14/20 Test #14: DataRetrievalTest.GetAllVoltages ............   Passed
15/20 Test #15: DataRetrievalTest.MeasureCurrent ............   Passed
16/20 Test #16: DataRetrievalTest.MeasureIllumination .......   Passed
17/20 Test #17: DataRetrievalTest.MeasureLeakage ............   Passed
18/20 Test #18: DataRetrievalTest.Gaussian1DMeasureGetSet ...   Passed
19/20 Test #19: DataRetrievalTest.VoltageSweepCurrent .......   Passed
20/20 Test #20: DataRetrievalTest.VoltageSweepCurrent2D .....   Passed

100% tests passed, 0 tests failed out of 20
```

## Current Compatibility Contract

The working contract after the refactor is:

- Controller tests continue to talk to the hub through NATS.
- The hub talks to ISS through gRPC/protobuf.
- ISS executes Lua scripts through `RuntimeContext`.
- Lua instrument libraries use `instrument_call_stack` userdata.
- Channel-group commands include channel in both:
  - call-stack metadata
  - command parameter table
- ISS prevents duplicate implicit channel injection.
- ISS preserves API-declared parameter order.
- Workers validate against generated instrument APIs.
- Plugins consume `channel` as the channel parameter name.
- Plugins return values through `plugin_response_push`.
- The hub converts ISS call results and buffers back into controller-facing
  NATS measurement responses.

That full chain is what made the final 20/20 controller run pass.
