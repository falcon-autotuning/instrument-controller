# Falcon Routine Port Payload Refactor Plan

## Document Status

- Last updated: 2026-08-29
- Primary test file: `tests/instrument-control/data-retrieval.cpp`
- New falcon-routine API under evaluation: `request_port_payload(int timeout_ms)`
- Current build mode: local `falcon-routine` checkout through `LOCAL_FALCON_ROUTINE=ON`
- Build status: compiling and linking successfully
- Runtime test status: focused `DataRetrievalTest.SetVoltage` starts successfully
  through the release falcon-routine v1.0.7 path and resolves the source-voltage
  port, but ISS still rejects the underlying setter invocation because its
  packaged channel-parameter contract does not match the generated Lua

## Objective

Update the instrument-controller integration tests and the hub-facing workflow to
use the current falcon-routine contract instead of the deprecated convenience
functions `request_knob(...)` and `request_meter(...)`.

The intended long-term flow is:

1. ISS loads an instrument API and its plugin.
2. The hub obtains the instrument's physical inputs, outputs, commands, and
   setting capabilities from ISS.
3. The hub maps the physical instrument information through the device wiremap.
4. The hub publishes the mapped physical knobs and meters through its port
   request endpoint.
5. `falcon-routine::request_port_payload(...)` requests the complete mapped port
   payload once.
6. Falcon or an integration test selects the required ports from that payload.
7. Measurement requests carry those hub-owned ports back to the hub.
8. The hub uses the selected Lua/Teal schema and ISS command metadata to execute
   measurements or settings.

The hub should remain the authority for instrument metadata. Tests should not
reconstruct physical port metadata that the hub already owns.

## Contract Boundaries

### Physical port payload

`PORT_PAYLOAD` remains the discovery mechanism for physical device-facing ports:

- knobs are writable physical outputs
- meters are readable physical inputs
- wiremap pseudo-names identify the device connection associated with each port
- the hub owns canonical names, instrument types, units, descriptions, and roles

The current falcon-routine return type is:

```cpp
std::tuple<Ports, Ports> request_port_payload(int timeout_ms);
```

Tuple element `0` contains knobs and tuple element `1` contains meters.

### Instrument settings

Instrument settings such as sample rate, averaging bins, slope, and trigger
leader should not be added to the physical port payload merely to support test
construction.

Settings should be managed by the hub through schema-selected Lua/Teal scripts
and ISS command metadata. The expected scoping rule is:

- a setting command with a `channel_group` is channel-scoped
- a setting command without a `channel_group` is instrument-scoped

The exact request-side representation of a setting is still a design decision.
Until that contract exists, test-side setting ports remain isolated compatibility
shims rather than being presented as real hub-discovered ports.

## Deprecated Behavior

The deprecated falcon-routine header supplied these inline helpers:

- `find_port_in_payload(...)`
- an overloaded `request_port_payload(...)` that selected one port
- `request_knob(...)`
- `request_meter(...)`

Those helpers fetched the complete payload for each lookup and then selected a
single port by role, canonical name, and pseudo-name.

The current falcon-routine header exposes only the complete payload request. This
is a useful boundary: the client can request once, cache for the lifetime of the
test, and perform deterministic local lookups without repeated NATS requests.

## Refactor Phases

### Phase 1: Align with the current falcon-routine API

Status: completed at the port-payload layer; focused runtime validation reaches
the `SetVoltage` command.

Work:

- request `PORT_PAYLOAD` through the current public API
- store the returned tuple in the fixture
- request it once after the hub-ready signal
- release it before stopping the hub
- stop calling deprecated `request_knob(...)` and `request_meter(...)`

Acceptance criteria:

- `data-retrieval.cpp` contains no calls to `falcon::routine::request_knob`
- `data-retrieval.cpp` contains no calls to `falcon::routine::request_meter`
- the integration test executable compiles with falcon-routine v1.0.6/local HEAD
- one payload request is made per test fixture lifecycle

### Phase 2: Use hub-owned metadata for physical ports

Status: completed for the source-voltage knob path; remaining physical meter
lookups still require runtime validation.

Work:

- add local lookup helpers over the cached payload
- distinguish knobs and meters with `is_knob()` and `is_meter()`
- match ports using both `default_name()` and `pseudo_name()`
- include available ports in lookup failure diagnostics
- replace fabricated physical getters with payload lookups
- retain synthetic ports only for unresolved setting capabilities

Acceptance criteria:

- physical source voltage setters come from `PORT_PAYLOAD`
- physical source measured-voltage getters come from `PORT_PAYLOAD`
- physical multimeter voltage and stream getters come from `PORT_PAYLOAD`
- a missing port failure reports the requested role, name, pseudo-name, and the
  available payload entries

### Phase 3: Define the ISS-to-hub capability contract

Status: not started.

Work:

- define an ISS endpoint or response that exposes loaded command descriptors
- include command name, parameters, outputs, units, data types, and channel group
- distinguish physical IO roles from instrument setting roles
- preserve `channel_group` so the hub can distinguish channel-scoped settings
  from instrument-scoped settings
- define how the hub resolves a schema input to an ISS command parameter
- decide whether capability information is requested at startup, cached, and
  invalidated when instruments are recreated

The physical `PORT_PAYLOAD` should not be expanded with synthetic setting ports
as a shortcut for this phase.

Acceptance criteria:

- the hub can list the setters/getters supported by every loaded instrument
- setting scope comes from API command metadata, not naming conventions
- Lua/Teal measurement dispatch can resolve setting commands without test-created
  instrument metadata
- malformed or incomplete API definitions fail with actionable diagnostics

### Phase 4: Remove test-side setting shims

Status: not started.

Current shims:

- `BuildSettingPort(...)`
- `BuildSettingGetterPort(...)`

Work:

- migrate sample-rate commands to the hub capability contract
- migrate number-of-samples/bins commands
- migrate slope commands
- migrate trigger-leader commands
- remove synthetic setting readback ports
- remove synthetic setting setter ports

`BuildSettingPort(...)` may no longer be needed after Phase 3. That should be
confirmed by implementing one channel-scoped setting and one instrument-scoped
setting through the final hub contract before deleting the helper.

Acceptance criteria:

- no test manually constructs an `InstrumentPort` for an ISS-backed capability
- setting tests still express their intended target or channel
- both channel-scoped and instrument-scoped setting tests pass

### Phase 5: Move reusable payload selection upstream

Status: not started and optional.

The local test lookup helper is intentionally small while the contract settles.
Once the matching rules are stable, move a reusable selector into falcon-routine
or falcon-core instead of maintaining equivalent client implementations.

Possible API:

```cpp
InstrumentPortSP find_port(
    const std::tuple<Ports, Ports>& payload,
    PortRole role,
    const std::string& canonical_name,
    const ConnectionSP& pseudo_name);
```

This helper should not issue network requests. Payload acquisition and payload
selection should remain separate operations.

Acceptance criteria:

- selection semantics are tested in the owning upstream repository
- instrument-controller removes its private equivalent
- payload lookup remains deterministic and network-free

### Phase 6: Complete integration and regression coverage

Status: not started.

Work:

- validate every physical port used by the 20 schema tests
- add a focused payload contract test before measurement tests
- assert that knobs and meters have expected role, units, instrument type, and
  pseudo-name
- add negative tests for missing and ambiguous ports
- validate one channel-scoped and one instrument-scoped setting capability
- run tests individually and as a complete suite
- retain per-test process and temporary-directory isolation

Acceptance criteria:

- all integration tests pass individually
- all integration tests pass in one `make test` run
- no test receives stale payload or measurement data from another test
- no hub, ISS daemon, or ISS worker process survives fixture teardown

## Work Accomplished

### Local falcon-routine build path

The instrument-controller build can select the sibling falcon-routine checkout
with:

```bash
make build PRESET=linux-clang-release LOCAL_FALCON_ROUTINE=ON
```

This allows the current API and dependency fixes to be tested without publishing
a new falcon-routine release.

### Cached payload fixture state

`DataRetrievalTest` now owns a `std::unique_ptr<PortPayload>`. `SetUp()` requests
the payload after the hub reports ready, and `TearDown()` clears it before the
hub is stopped.

### Local payload lookup

`FindPayloadPort(...)` now:

- selects the knob or meter collection
- rejects null entries
- validates the port's intrinsic role
- matches the hub's canonical `default_name()`
- matches the mapped `pseudo_name()` connection
- reports all available ports when lookup fails

`LookupKnobPort(...)` and `LookupMeterPort(...)` are now local wrappers around
that cached lookup.

### Removed deprecated calls

All direct calls to the deprecated falcon-routine helpers were removed from
`data-retrieval.cpp`.

The buffered and compatibility tests now use the same cached lookup path as the
schema tests.

### Removed fabricated physical getters

The following physical getters now come from the hub payload:

- source measured voltage
- multimeter scalar voltage
- multimeter buffered stream

Synthetic getter construction is retained only for setting readbacks that the
current hub/ISS capability contract does not yet expose.

### Setting workaround made explicit

`BuildGetterPort(...)` was renamed to `BuildSettingGetterPort(...)` so its narrow
purpose is visible. A comment marks it as temporary and ties its removal to the
future capability contract.

### Compile verification

The integration test executable compiled and linked successfully against the
current local falcon-routine API using the local build feature.

This verifies source-level completion of Phases 1 and 2. The focused
`SetVoltage` test also verifies that `PORT_PAYLOAD` can be requested and that the
source-voltage knob can be selected from it by role, canonical name, and mapped
pseudo-name. It does not yet verify successful execution by the voltage-source
plugin.

## Resolved ISS API Compatibility Blocker

The initial test run failed before this line was reached:

```cpp
falcon::routine::request_port_payload(TIMEOUT_MS)
```

ISS v2.0.14 rejects the mock instrument API while starting `Meter1`. Its packaged
parser requires non-VISA protocols to use this shape:

```yaml
protocol:
  type: Custom
  name: MockMultimeter
```

The controller APIs previously used the plugin name directly as `type`:

```yaml
protocol:
  type: MockMultimeter
```

The two generated mock APIs were temporarily edited to use `type: Custom` and
the plugin protocol identifier as `name`. That was not a durable change. Every
test fixture calls `ExtendInstrumentApis(...)`, which regenerates those files
from:

- `tests/instrument-apis/multimeter-api.yml.tmpl`
- `tests/instrument-apis/source-api.yml.tmpl`

The focused rerun therefore restored the legacy protocol shape before starting
the hub. The generated files currently contain the old form, and the worker
proxy reported:

```text
Missing required api field: protocol name
```

Both templates were updated to emit `type: Custom` plus `protocol.name`. Their
generated outputs now preserve the v2.0.14 contract.

After this fix, `DataRetrievalTest.SetVoltage` passed in a focused release-path
run using installed falcon-routine v1.0.7. The earlier timeout and hub shutdown
messages were consequences of ISS rejecting the generated API, not failures in
the cached port-payload implementation.

That gtest result is currently a false positive for command execution. The
generated source Lua sends a table containing `channel`, as declared by
`channel_parameter.name` in the instrument API. Packaged ISS v2.0.14 instead
registers the injected channel parameter under the channel-group name `analog`,
so it reports:

```text
Missing required parameter 'analog' for command Source1.SET_VOLTAGE
```

ISS then returns zero measurement results without surfacing an RPC error. The Go
measurement handler accepts that result, updates its in-memory voltage cache,
and constructs a response from the requested value. This lets the assertion pass
without the mock plugin receiving `SET_VOLTAGE`.

The intended contract remains the API-declared parameter name `channel`. The
durable fix is to publish an ISS version that preserves
`channel_parameter.name`, update mock plugins to consume `channel`, and ensure a
failed setter invocation is propagated to the hub instead of being converted to
a successful echoed response. Changing the API templates to call the parameter
`analog` would only be a compatibility workaround for ISS v2.0.14.

An additive `teal-api-gen` compatibility experiment emitted both the
API-declared channel parameter and the channel-group alias in named command
payloads. That approach is not compatible with packaged ISS v2.0.14. After the
table parser selects the `analog` alias, `RuntimeContext` unconditionally inserts
the call stack channel as another `analog` parameter. `SET_VOLTAGE` consequently
reaches the worker with three parameters instead of the two declared by the
command, while channel-only getters arrive with two instead of one. The worker
rejects the command and does not acknowledge the validation failure, producing
a five-second proxy timeout and no hub measurement response.

The dual-alias generator change should not be released as the final fix. The
durable path is to use the corrected ISS channel parser, retain the API-declared
`channel` payload name, update mock plugins to the same name, and make worker
validation failures return an error response instead of timing out.

## Resume Checklist

1. Run a focused test that uses a physical meter from `PORT_PAYLOAD`.
2. Run the complete suite through `LOCAL_FALCON_ROUTINE=OFF`.
3. Classify any remaining failures as payload lookup, measurement dispatch, or
   setting-shim failures.
4. Continue with Phase 3 after the physical knob and meter payload paths both
   pass at runtime.

## Next Validation Boundary

The source-voltage knob path now passes. The next useful result will be one of:

- a physical meter lookup succeeds, completing basic knob/meter payload coverage
- a meter lookup fails with the available-port diagnostic, identifying a
  canonical name, role, or pseudo-name mismatch
- a setting test fails at one of the intentionally retained test-side shims

Each result is narrower and more actionable than restoring the deprecated
single-port request helpers.
