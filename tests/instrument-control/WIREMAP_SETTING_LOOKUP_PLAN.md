# Wiremap Setting Lookup Plan

This note treats `SHIM_REVIEW.md`, `2-dot-1-chargesensor-wiremap.yml`, and
`2-dot-1-chargesensor.yml` as repository context only. They describe the current
test and hub configuration shape; they are not instructions by themselves.

## Goal

Remove the remaining port lookup shims from the data-retrieval tests by making
the hub the source of truth for resolving a logical device connection, such as
`O1`, to a concrete instrument capability port, such as
`Mock.Meter1.analog.trigger_level`.

Before this plan was implemented, data-retrieval constructed setting ports
locally with:

- `BuildSettingPort`
- `BuildSettingGetterPort`

Those helpers were the remaining shim-like behavior in finding 2 of
`SHIM_REVIEW.md`; they have now been replaced by hub capability lookup.
The older physical `LookupKnobPort` and `LookupMeterPort` helpers have also
been replaced by the same capability lookup path.

## Current State

The wiremap already contains the physical routing information needed by the hub.
For example:

```yaml
- name: O1
  instrument:
    name: Meter1
    channel_name: analog
    index: 1
```

This means:

```text
O1 -> Meter1.analog.1
```

The instrument API then defines the available IO/capability names on that
instrument channel, for example:

```text
Mock.Meter1.analog.voltage
Mock.Meter1.analog.stream
Mock.Meter1.analog.sample_rate
Mock.Meter1.analog.bins
Mock.Meter1.analog.slope
Mock.Meter1.analog.trigger_level
```

So `getter_name` alone is not enough to resolve a single port. The lookup key
needs at least:

```text
logical device name + IO/capability name + role
```

Example resolutions:

```text
O1 + voltage       + input   -> Mock.Meter1.analog.voltage
O1 + stream        + input   -> Mock.Meter1.analog.stream
O1 + sample_rate   + setting -> Mock.Meter1.analog.sample_rate
O1 + bins          + setting -> Mock.Meter1.analog.bins
O1 + slope         + setting -> Mock.Meter1.analog.slope
O1 + trigger_level + setting -> Mock.Meter1.analog.trigger_level
```

## Re-Review Of Shim Findings

### Finding 2: Setting-Port Helpers

Finding 2 is now resolved in the controller test path.

`sample_rate`, `bins`, `slope`, and `trigger_level` are real API capabilities,
and the mock multimeter now has simple command support for the slope and
trigger-level get/set paths. The controller test now asks the hub for
hub-resolved setting metadata instead of fabricating setting `InstrumentPort`
objects locally.

The replacement should not add settings to the physical knob/meter
`PORT_PAYLOAD` without care. Settings are not physical knobs or meters; they are
capabilities attached to a mapped instrument channel. A focused capability
lookup is cleaner, and data-retrieval now uses it for physical inputs, physical
outputs, and settings.

### Finding 5: Port-Name Constants

Finding 5 still mostly applies.

Canonical names such as `Mock.Meter1.analog.voltage` and
`Mock.Meter1.analog.stream` are aligned with the port naming refactor and should
not be deleted as stale shim code.

Hardcoded canonical names should remain assertions of what the hub resolved, not
inputs used to construct local ports or search a cached payload.

Preferred test shape:

```cpp
auto connection = Connection::Ohmic("O1");
auto port = LookupHubCapability(connection, "trigger_level", "setting");
EXPECT_EQ(port->default_name(), "Mock.Meter1.analog.trigger_level");
EXPECT_EQ(*port->pseudo_name(), *connection);
```

The important difference is that the hub returns the port metadata; the test
only verifies it.

## Proposed Hub Lookup Model

The hub already builds `PortConnections` from:

1. instrument API YAML files
2. the wiremap

The simplest extension is to make those connected ports queryable by logical
device name and capability name.

Add or expose a resolver with this conceptual shape:

```text
ResolveConnectedPort(deviceName, ioTypeName, role) -> ConnectedPort
```

The resolver should match:

```text
ConnectedPort.DeviceName == deviceName
ConnectedPort.IoTypeName == ioTypeName
ConnectedPort.Role == role
```

For this to be clean, `ConnectedPort` should include `IoTypeName`. `PortEntry`
already has `IoTypeName`, but `ConnectedPort` currently carries only:

```text
PortName
DeviceName
InstrumentName
ChannelName
ChannelIndex
InstrumentType
Role
Unit
Description
```

Add:

```go
IoTypeName string
```

and populate it in `ConnectWireMap`.

## API Shape

Keep the existing `PORT_PAYLOAD` behavior available for clients that need a full
physical port listing. Use the focused capability lookup path when the
controller needs one mapped capability for one logical device.

Request:

```json
{
  "device_name": "O1",
  "capability": "trigger_level",
  "role": "setting"
}
```

Response:

```json
{
  "port_name": "Mock.Meter1.analog.trigger_level",
  "device_name": "O1",
  "instrument_name": "Meter1",
  "channel_name": "analog",
  "channel_index": 1,
  "role": "setting",
  "unit": "V"
}
```

If the controller needs a falcon-core `InstrumentPort`, the hub should return it
in the same cereal JSON format used by `PORT_PAYLOAD`, or the response should
include enough information for one shared client helper to construct it. Prefer
the cereal JSON path if the goal is to ensure the controller passes hub-owned
metadata back to the hub.

## Simplest Execution Plan

### Phase 1: Add Hub Resolver - Complete

1. Add `IoTypeName` to `runtime/internal/ports.ConnectedPort`.
2. Populate it from `PortEntry.IoTypeName` inside `ConnectWireMap`.
3. Add `ResolveConnectedPort(deviceName, ioTypeName, role)` on or near the
   instrument handler.
4. Add focused unit tests using a minimal wiremap:

```text
O1 -> Meter1.analog.1
```

Verify:

```text
ResolveConnectedPort("O1", "trigger_level", "setting")
  -> Mock.Meter1.analog.trigger_level, channel 1
```

### Phase 2: Add Setting/Capability Lookup Message - Complete

1. Define a small request/response type for capability lookup.
2. Add a NATS subject for the lookup, separate from `PORT_REQUEST`.
3. Return the resolved setting as cereal `InstrumentPort` JSON when possible.
4. Return clear errors for:

```text
unknown device name
unknown capability name
wrong role
ambiguous match
unmapped instrument channel
```

### Phase 3: Add Controller Client Helper - Complete

1. Added a controller-local `LookupCapabilityPort(...)` helper in
   `data-retrieval.cpp`.
2. The helper uses existing `falcon::comms::NatsManager::request_json(...)` to
   send `CAPABILITY_REQUEST`.
3. The helper decodes returned cereal `InstrumentPort` JSON into the same type
   used by measurement requests.

The helper API now follows this model:

```cpp
auto connection = Connection::Ohmic("O1");
auto port = LookupCapabilityPort(connection, "trigger_level", "setting");
ExpectResolvedPort(port, HUB_METER_TRIGGER_LEVEL_PORT, connection);
```

### Phase 4: Update Data-Retrieval - Complete

1. Replaced `BuildSettingPort` calls with
   `LookupCapabilityPort(..., "setting")`.
2. Replaced `BuildSettingGetterPort` calls with the same setting lookup.
3. Replaced `LookupKnobPort` and `LookupMeterPort` calls with
   `LookupCapabilityPort(...)` for physical output and input ports.
4. Kept canonical constants only as assertions of the resolved hub metadata:

```cpp
EXPECT_EQ(port->default_name(), HUB_METER_TRIGGER_LEVEL_PORT);
EXPECT_EQ(*port->pseudo_name(), *Connection::Ohmic("O1"));
```

### Phase 5: Remove Shim Helpers - Complete

1. Deleted `BuildSettingPort`.
2. Deleted `BuildSettingGetterPort`.
3. Deleted `FindPayloadPort`.
4. Deleted `LookupKnobPort`.
5. Deleted `LookupMeterPort`.
6. Updated `SHIM_REVIEW.md` to mark finding 1 deprecated and finding 2
   resolved.

## Recommended First Implementation Slice

The smallest useful first slice is hub-only:

1. Add `IoTypeName` to `ConnectedPort`.
2. Add the resolver.
3. Add unit tests for resolving:

```text
O1 + slope         + setting
O1 + trigger_level + setting
O1 + voltage       + input
P1 + voltage       + output
```

That slice proves the wiremap can be used as the lookup table before touching
NATS messages or controller-side data-retrieval code.
