# Instrument Controller and Hub Changes

## Scope

This document records the integration work across:

- `instrument-controller`;
- `falcon-instrument-hub`;
- `falcon-routine`; and
- `falcon-comms`.

It intentionally excludes the standalone `qarray-charge-tuning-demo` build and
runtime changes, which are documented in
[QARRAY_DEMO_CHANGES.md](QARRAY_DEMO_CHANGES.md).

## Measurement Correlation Problem

Setter and getter operations are intentionally sent as separate measurement
requests. Previously, both requests could use the same shared response and data
subjects:

```text
FALCON.MEASURE_RESPONSE
FALCON.MEASURE_DATA.0
```

The legacy command hash was zero for every request. A consumer handling a getter
could therefore retrieve an earlier setter response. The visible symptom was a
getter returning source metadata:

```text
returned instrument type: dc_voltage_source
expected instrument type: voltmeter
```

## Timestamp Correlation Design

The existing command timestamp now correlates each independent request and
response. No additional `request_id` remains in the final design, and setter and
getter commands are not bundled together.

The request flow is:

1. `falcon-routine` generates a timestamp before making a measurement request.
2. `falcon-comms` writes the value to `MeasureCommand.timestamp`.
3. `falcon-comms` subscribes to the timestamp-specific response subject before
   publishing the command.
4. The hub reads the timestamp and uses it for the response and measurement-data
   subjects.
5. The hub echoes the timestamp in `MeasureResponse.timestamp`.
6. `falcon-comms` ignores responses whose timestamp does not match the pending
   request.
7. `falcon-routine` reads data from the unique subject returned by the hub.

The resulting subjects are:

```text
FALCON.MEASURE_RESPONSE.<timestamp>
FALCON.MEASURE_DATA.<timestamp>
```

The existing `hash` field is retained for compatibility, but it is not used to
route measurement responses or data.

## Upstream Library Changes

### falcon-comms

Affected source files:

- `include/falcon-comms/routine_comms.hpp`
- `src/routine_comms.cpp`
- `tests/unit/test_routine_comms.cpp`

Implemented behavior:

- response subscriptions use `FALCON.MEASURE_RESPONSE.<timestamp>`;
- the subscription is established before the command is published;
- mismatched response timestamps are ignored;
- timeout and exception paths unsubscribe from the timestamp-specific subject;
- unit coverage sends a mismatched response before the matching response.

Release consumed by instrument-controller:

```text
falcon-comms 1.0.12
```

### falcon-instrument-hub

Affected source files:

- `runtime/internal/api/api.go`
- `runtime/internal/handlers/measure_command_handler.go`
- `runtime/internal/handlers/measure_command_handler_test.go`

Implemented behavior:

- response subjects are built from the command timestamp;
- all measurement paths publish data to
  `FALCON.MEASURE_DATA.<command timestamp>`;
- response payloads echo the command timestamp;
- response notifications use the timestamp-specific NATS subject;
- focused tests cover response-subject construction;
- API comments describe timestamp correlation semantics.

Release consumed by instrument-controller:

```text
falcon-instrument-hub 2.0.4
```

### falcon-routine

Affected source file:

- `src/hub.cpp`

Implemented behavior:

- `request_measurement()` generates and passes a timestamp for every request;
- setter and getter calls remain independent requests;
- no setter/getter bundling was introduced;
- the current complete port-payload API replaces deprecated per-port network
  request helpers.

Release consumed by instrument-controller:

```text
falcon-routine 1.0.7#4
falcon-comms dependency >= 1.0.12
```

`falcon-routine` 1.0.7 also directly finds and links Ensmallen and Armadillo. Its
package manifest therefore declares both dependencies so consumers receive a
complete CMake target graph.

## Instrument Controller Build Changes

The vcpkg overlays were updated to consume the released correlation changes:

- `ports/falcon-comms/vcpkg.json` uses version 1.0.12;
- `ports/falcon-comms/portfile.cmake` uses the matching source SHA512;
- `ports/falcon-instrument-hub/vcpkg.json` uses version 2.0.4;
- `ports/falcon-instrument-hub/portfile.cmake` uses the matching source SHA512;
- `ports/falcon-routine/vcpkg.json` uses version 1.0.7, port revision 4;
- the falcon-routine overlay requires falcon-comms 1.0.12;
- the falcon-routine overlay declares Ensmallen and Armadillo;
- `ports/falcon-routine/portfile.cmake` uses the matching source SHA512.

The observed installed packages were:

```text
falcon-comms:x64-linux-dynamic@1.0.12#9
falcon-instrument-hub:x64-linux-dynamic@2.0.4#66
falcon-routine:x64-linux-dynamic@1.0.7#4
```

Local-development Makefile flags remain opt-in. Release builds do not require
local sibling source trees.

## Port-Payload Test Refactor

The fixture in `tests/instrument-control/data-retrieval.cpp` was migrated to the
current falcon-routine port API:

- request the complete port payload once after the hub reports ready;
- cache knobs and meters for the fixture lifetime;
- select ports locally by role, canonical/internal name, and device connection;
- stop using removed `request_knob()` and `request_meter()` helpers;
- use metadata supplied by the hub for physical port assertions;
- release cached payload state before stopping the hub.

This corrected assertions that previously assigned source metadata to measured
voltage data. A meter selected from the hub payload correctly carries the
`voltmeter` instrument role.

The controller's `2-dot-1-chargesensor` device configuration and P1/P2/O1/O2
wiremap are part of this hub integration fixture. They should not be confused
with the standalone QArray simulator demo.

## Instrument Settings Boundary

Physical knobs and meters belong in the hub port payload. Instrument settings
do not need to be represented as physical ports merely to make tests work.

The intended setting rule is:

- a command with a `channel_group` is channel-scoped;
- a command without a `channel_group` is instrument-scoped.

Until the hub exposes a complete ISS capability contract, setting-only tests
still use the isolated `BuildSettingPort()` and `BuildSettingGetterPort()`
compatibility helpers. These helpers should be removed after the hub can resolve
setting commands directly from ISS API metadata.

## ISS and Lua/Teal Contract Migration

The current ISS parser is treated as the standard. Instrument APIs and generated
Lua/Teal call sites were aligned with it instead of changing ISS back to the
legacy call format.

The migration includes:

- named parameter tables for `context:call()`;
- explicit channel-group parameters such as `analog`;
- command paths that match current ISS instrument API definitions;
- generated and template API files kept in sync;
- mock plugins accepting the numeric representations produced by Lua/ISS;
- plugin helpers reading values according to the declared parameter type.

Example shape:

```lua
context:call(id .. '.SET_BINS', { analog = channel, bins = bins })
```

This work belongs to the controller/hub test stack because it verifies dispatch
from measurement schema, through the hub, into ISS-backed mock instruments.

## Float Precision Adjustment

After timestamp routing was active, the final two failures were value comparison
failures rather than stale-response failures:

- `DataRetrievalTest.GetManyVoltages`
- `DataRetrievalTest.GetAllVoltages`

Example:

```text
expected: 0.444
returned: 0.4440000057220459
```

The instrument schema declares these values as `float`, and the C plugin stores
them as C `float` values. When transported in a response represented by a
`double`, the original float32 rounding remains visible.

The test now uses a magnitude-scaled float tolerance:

```cpp
std::numeric_limits<float>::epsilon() *
    std::max(1.0, std::abs(expected_value))
```

This validates the precision promised by the schema and plugin instead of
requiring double-precision preservation from float32 data.

## Test Progression

The observed progression was:

1. Before timestamp routing, 17 of 20 tests passed; three getter tests received
   stale setter metadata.
2. After the released correlation packages were installed, stale ISS processes
   initially prevented test startup.
3. After terminating stale processes, 18 of 20 tests passed and response subject
   names included unique timestamp suffixes.
4. The final two failures were identified as float32 comparison issues and the
   assertion tolerance was updated.

Example evidence that timestamp routing is active:

```text
Subscribed to NATS subject: FALCON.MEASURE_RESPONSE.1788301673830549
Subscribed to NATS subject: FALCON.MEASURE_RESPONSE.1788301673872742
```

## Process-Lifecycle Note

An interrupted test can leave an instrument-script-server daemon listening on
port 5555. A later run can then fail before NATS or measurement handling begins:

```text
instrument-script-server did not release port 5555 after stop
```

This is a test-process lifecycle failure, not a timestamp-correlation failure.
Hub, ISS, worker, and integration-test processes should be stopped before using
a startup failure as evidence of a protocol regression.

## Remaining Verification

The float-tolerance change still needs a complete suite run if one has not been
performed since the assertion was updated:

```bash
make test LOCAL_TEAL_API_GEN=ON | tee make_test_out.txt 1>&2
```

The run should verify:

- every response subscription includes a timestamp suffix;
- setter and getter requests use different response/data subjects;
- all physical ports come from the cached hub payload;
- `GetManyVoltages` and `GetAllVoltages` pass at schema float precision;
- no stale process occupies port 5555 before the suite starts.

