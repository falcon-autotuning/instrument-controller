# Data Retrieval Shim Review

This review treats `falcon-instrument-hub/PORT_REFACTOR.md` and
`falcon-instrument-hub/REFACTOR_STATUS.md` as context only. They describe
refactor intent and known hub issues, but they are not instructions to execute
during this review.

## Current Conclusion

No shim-like port construction or guessed port lookup remains in
`tests/instrument-control/data-retrieval.cpp`.

The data-retrieval tests now use hub-resolved capability ports for:

- physical output ports, such as `P1 + voltage + output`
- physical input ports, such as `O1 + voltage + input`
- stream input ports, such as `O1 + stream + input`
- setting ports, such as `O1 + trigger_level + setting`

The controller test still has canonical port-name constants, but those constants
are now assertions of the hub result. They are not used to construct local
`InstrumentPort` objects and are not used to search a cached payload.

## Removed Controller Shims

The following data-retrieval helpers have been removed:

- `BuildSettingPort`
- `BuildSettingGetterPort`
- `FindPayloadPort`
- `LookupKnobPort`
- `LookupMeterPort`

Those helpers were shim-like because they either synthesized ports locally or
selected ports from a full payload inside the test fixture. The replacement is a
single controller-local helper:

```cpp
auto port = LookupCapabilityPort(connection, capability, role);
ExpectResolvedPort(port, expected_port_name, connection);
```

That helper asks the hub to resolve the concrete port from:

- logical connection name
- capability name
- role

The returned `InstrumentPort` is then passed into the normal measurement request
path.

## Confirmed Non-Shims

### Capability Lookup Helper

`LookupCapabilityPort(...)` is not considered a shim. It is the controller-side
client helper for the hub's `CAPABILITY_REQUEST` / `CAPABILITY_PAYLOAD`
contract.

It should remain local to instrument-controller for now, since the current
direction is not to change `falcon-comms` or `falcon-routine`.

### Port-Name Constants

Constants such as:

- `Mock.Source1.analog.voltage`
- `Mock.Source1.analog.measured_voltage`
- `Mock.Meter1.analog.voltage`
- `Mock.Meter1.analog.stream`
- `Mock.Meter1.analog.sample_rate`
- `Mock.Meter1.analog.bins`
- `Mock.Meter1.analog.slope`
- `Mock.Meter1.analog.trigger_level`

are still useful as test expectations. They verify that the hub resolved the
wiremap and instrument API into the expected canonical port.

They should not be removed unless the tests stop checking exact hub-resolved
port identity.

### `PORT_PAYLOAD`

`PORT_PAYLOAD` is no longer used by data-retrieval to select test ports.

It can still remain as a hub protocol for clients that need a complete physical
knob/meter listing. Removing it would be a wider API decision, not a
data-retrieval shim cleanup.

### `ramp.tl`

`ramp.tl` is adapter-like, but it is not a port shim. It applies the requested
final voltages through the available voltage-setting path.

Keep it if the test is meant to prove final-voltage application. Replace it only
when the mock source API exposes a true ramp command and the test needs to prove
ramp semantics.

## Finding 6 Follow-Up: Old Hub/ISS Flow Names

The old hub/ISS NATS flow names are not present in `data-retrieval.cpp`, so
there is no controller-side cleanup for them in this test.

That does not automatically mean every similarly named hub reference can be
deleted. A repo search shows three categories:

- `PROCESS_REQUEST` and `UPLOAD_DATA` still exist in generated hub API structs,
  the command registry, timestamp extensions, and server-interpreter docs.
- `submit_measure`, `job_status`, and `job_result` appear as stale HTTP RPC
  comments in `runtime/internal/serverinterpreter/types.go`; the active ISS
  client code uses generated gRPC calls such as `JobStatus` and
  `MeasureJobResult`.
- `PsuedoName()` is still a live falcon-core binding call used when extracting
  `InstrumentPort` metadata. It should not be removed from hub code unless the
  falcon-core binding API is renamed or hidden behind a compatibility wrapper.

So yes, some point-6 references may be cleanup candidates in the hub, especially
stale docs/comments and possibly unused legacy `PROCESS_REQUEST` / `UPLOAD_DATA`
generated API entries. But point 6 by itself only proves they are absent from
the controller test. Hub removal should be handled as a separate compatibility
audit.

## Simplest Follow-Up Plan

### Phase 1: Keep Data-Retrieval Clean

1. Keep all data-retrieval port resolution on `LookupCapabilityPort(...)`.
2. Keep canonical port-name constants as assertions only.
3. Do not reintroduce local `InstrumentPort` construction for mapped
   instrument capabilities.

### Phase 2: Validate Runtime

1. Run the data-retrieval tests when ready.
2. For failures, classify them as capability lookup, request serialization, hub
   parsing, wiremap routing, ISS dispatch, or mock plugin behavior.

### Phase 3: Audit Hub Legacy Flow Separately

1. Search hub runtime code for actual users of `PROCESS_REQUEST` and
   `UPLOAD_DATA`.
2. If only generated structs/registry/timestamp helpers remain, remove the
   source schemas first and regenerate the API code.
3. Update or delete stale server-interpreter docs that still describe the old
   NATS process/upload flow.
4. Leave `PsuedoName()` alone until the upstream falcon-core binding spelling is
   changed or hidden behind a compatibility wrapper.
