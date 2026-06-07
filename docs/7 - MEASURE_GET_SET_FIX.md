# Measure Get/Set Fix

## Summary

`DataRetrievalTest.Gaussian1DMeasureGetSet` began passing after fixing how the
`measure_get_set` and `set_voltage` scripts call the generated instrument Lua
helpers.

The hub-side measurement handler was not the final cause of the failure in the
passing run. The runtime and instrument-server logs showed that the scripts were
still receiving full instrument IDs such as `Source1` and `Meter1`. The actual
failure happened later inside the generated multimeter helper when
`measureStream()` received a `nil` channel.

## Root Cause

The generated instrument helper APIs expect instrument calls in this shape:

- `Mock1Source1:setVoltage(id, channel, voltage)`
- `Mock5Meter1:setSampleRate(id, channel, sampleRate)`
- `Mock5Meter1:setBins(id, channel, bins)`
- `Mock5Meter1:measureStream(id, channel)`

Before the fix, the `measure_get_set` and `set_voltage` scripts were passing
only the channel value to those helpers in some places. That caused the channel
number to be treated as the instrument ID, while the real `channel` argument
became `nil`.

This matched the runtime error seen in the failing run:

- `attempt to compare nil with number`
- inside `measureStream`

That error came from the generated multimeter helper trying to clamp a `nil`
channel before sending the command.

## Files Changed

### [`measure_get_set.tl`](/home/zdm2/Documents/github/FAlCon/instrument-controller/tests/data-retrieval-1D/measurement-scripts/measure_get_set.tl)

Updated the Teal source so all generated helper calls pass both `id` and
`channel`:

- `Mock1Source1:setVoltage(setter.id, setter.channel, voltage)`
- `Mock5Meter1:setSampleRate(getter.id, getter.channel, sampleRate)`
- `Mock5Meter1:setBins(getter.id, getter.channel, numPoints)`
- `Mock5Meter1:measureStream(getter.id, getter.channel)`

This is the source-of-truth change for the measurement script used by
`Gaussian1DMeasureGetSet`.

### [`measure_get_set.lua`](/home/zdm2/Documents/github/FAlCon/instrument-controller/tests/lua/measure_get_set.lua)

Applied the same fix in the compiled Lua output so the current test run picks up
the corrected call signature immediately.

This mattered because the integration test executes the Lua script from
`instrument-controller/tests/lua/`.

### [`set_voltage.tl`](/home/zdm2/Documents/github/FAlCon/instrument-controller/tests/data-retrieval-1D/measurement-scripts/set_voltage.tl)

Updated the Teal source for consistency:

- `Mock1Source1:setVoltage(setter.id, setter.channel, setVoltage)`

This prevents the same helper-signature bug from appearing in the standalone
set-voltage script path.

### [`set_voltage.lua`](/home/zdm2/Documents/github/FAlCon/instrument-controller/tests/lua/set_voltage.lua)

Applied the same correction in the compiled Lua output used by tests.

## Why The Test Passed Afterward

Once the scripts passed both the instrument ID and channel to the helper
functions:

- `Source1` remained the instrument ID
- the numeric channel stayed in the channel slot
- `Mock5Meter1:measureStream(...)` received a valid channel instead of `nil`
- the script could configure the mock meter and collect the buffered
  measurement successfully

In short: the fix was a script/helper argument mismatch, not an instrument-name
truncation issue in the handler.
