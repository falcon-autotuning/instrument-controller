# Port Payload Refactor Notes

**Context**: `DataRetrievalTest.Gaussian1D` manually constructs `independantKnob` and `getter`
ports rather than fetching them from the hub via `request_port_payload()`. The TODO comment at
`instrument-controller/tests/data-retrieval-1D/data-retrieval.cpp` lines ~436–445 tracks this.

```cpp
// TODO(port_payload): These ports are manually constructed rather than
// fetched from the hub via the PORT_PAYLOAD protocol
// (RuntimeComms::subscribe_port_payload / FALCON.PORT_PAYLOAD NATS subject).
// Once port_payload serialization is stable end-to-end, replace this block
// with a request_port_payload() call and remove the manual construction.
InstrumentPortSP independantKnob = InstrumentPort::Knob(
    "analog4_voltage", Connection::PlungerGate(DEPENDANT_NAME),
    InstrumentTypes::DC_VOLTAGE_SOURCE, SymbolUnit::Volt(),
    "Mock voltage source knob");
InstrumentPortSP clock = InstrumentPort::ExecutionClock();
```

---

## Blocking Issues

Before `request_port_payload()` can replace the manual construction, four structural
mismatches must be resolved:

### 1. Port name format mismatch

The hub publishes port payloads keyed by an internal name that does not match the
`"analog4_voltage"` / `"analog1_stream"` string identifiers used when constructing
`InstrumentPort` objects in the test. Until the hub and the C++ client agree on the
canonical port name format, looking up a port by name will either fail silently or
return the wrong port.

**What needs to change**: Audit how the hub names ports in the `FALCON.PORT_PAYLOAD`
NATS subject and align `request_port_payload()` call-sites to use the same name.

---

### 2. `InstrumentType` not mapped in PORT_PAYLOAD response

The `InstrumentType` enum value (e.g. `InstrumentTypes::DC_VOLTAGE_SOURCE`,
`InstrumentTypes::VOLTMETER`) is not reliably round-tripped through the PORT_PAYLOAD
JSON serialisation. The deserialised `InstrumentPort` comes back with a default or
unknown type, breaking any downstream code that inspects `instrument_type()`.

**What needs to change**: Verify that `InstrumentType` serialisation/deserialisation
is complete in `falcon-comms` and that the hub sets the field correctly before
publishing `PORT_PAYLOAD`.

---

### 3. Units mismatch — Volt vs MilliVolt

The hub currently returns `SymbolUnit::Volt()` for the getter port, but the test
constructs the getter with `SymbolUnit::MilliVolt()`. The downstream assertion:

```cpp
EXPECT_EQ(*labelledArray->units(), *SymbolUnit::MilliVolt())
```

…would fail if the units came from `request_port_payload()` without correction.

**What needs to change**: Either (a) the hub publishes `MilliVolt` for this port in
the instrument config/wiremap, or (b) `request_port_payload()` allows a units override
at the call site, or (c) the test expectation is updated once the hub-side units are
confirmed correct.

---

### 4. Ambiguous I/O type selection

`request_port_payload()` today does not have a way to distinguish between input and
output roles for a port that could serve both (e.g. a source that can also sense).
The test explicitly constructs an input knob and a separate output meter. When
fetching from the hub there is no unambiguous selector to request "the knob variant"
vs "the meter variant" of a physical connector that appears in both roles.

**What needs to change**: The PORT_PAYLOAD API or its call convention needs a role
discriminator (e.g. an `io_type` parameter or a dedicated `request_knob()` /
`request_meter()` helper) before the manual construction can be dropped.

---

## Suggested Refactor Target

Once all four issues are resolved, the manual block should collapse to something like:

```cpp
InstrumentPortSP independantKnob =
    request_port_payload(DEPENDANT_NAME, IoType::Knob, TIMEOUT_MS);
InstrumentPortSP getter =
    request_port_payload(GETTER_NAME, IoType::Meter, TIMEOUT_MS);
InstrumentPortSP clock = InstrumentPort::ExecutionClock();
```

The `ExecutionClock` has no hub-side representation and will always be constructed
locally regardless of how the other ports are fetched.

---

## Related Files

| File | Relevance |
|------|-----------|
| `instrument-controller/tests/data-retrieval-1D/data-retrieval.cpp` | Contains the TODO block |
| `falcon-comms/include/falcon-comms/runtime_comms.hpp` | `subscribe_port_payload` declaration |
| `falcon-comms/src/routine_comms.cpp` | `subscribe_port_payload` implementation (local override) |
| `falcon-instrument-hub/runtime/internal/handlers/` | Hub-side PORT_PAYLOAD publisher |
