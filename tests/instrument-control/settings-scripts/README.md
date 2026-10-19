# Plain Lua settings scripts

The six `.tl` basenames have matching `.lua` implementations. They use the
native ISS bindings directly and do not load generated instrument libraries.
The `.tl` files remain historical references; they are not compiled for this path.

| Script | Arguments after `ctx` | ISA command |
| --- | --- | --- |
| `get_voltage.lua` | `getter` | `GET_VOLTAGE` |
| `set_voltage.lua` | `setter, setVoltage` | `SET_VOLTAGE` |
| `get_sample_rate.lua` | `getter` | `GET_SAMPLE_RATE` |
| `set_sample_rate.lua` | `setter, sampleRate` | `SET_SAMPLE_RATE` |
| `get_number_of_samples.lua` | `getter` | `GET_BINS` |
| `set_number_of_samples.lua` | `setter, numberOfSamples` | `SET_BINS` |

The hub currently selects `get_source_voltage.lua` / `set_source_voltage.lua`
for its `source_voltage` characteristic. Those two additional standalone files
perform the same direct calls as `get_voltage.lua` / `set_voltage.lua`.

Targets must be native `InstrumentTarget` userdata. ISS deserializes
`LUA_TYPES_TARGET` parameters before calling `main`; the scripts use
`get_instrument_name()`, `get_channel_group()`, and `get_channel()`. The target
and call-stack Lua modules do not export a Lua `deserialize` function, so do not
pass serialized strings directly to these functions. A standalone Lua caller
can construct a target with `instrument_target.new({instrument=..., group=...,
channel=...})` after loading the native module.

Each script constructs a native call stack with
`instrument_call_stack.new(...)` and invokes `ctx:call(...)`. ISS collects the
ISA response for the hub to decode into `SettingResponse`. The scripts do not
construct a Falcon measurement response or echo a supplied getter value.
Scalar settings have no `InstrumentDomain` argument; domain bindings are used
by the sweep scripts that actually accept domains.

Voltages use V and the mock sample-rate ISA uses Hz. Sample rate and sample
count are converted to positive Lua integers because the mock ISA expects
integer parameters; this also handles the hub encoding sample rate as a double.
Setter target arguments are named `setter` to match the hub manifest, including
the meter setters that the older Teal sources named `getter`.

Stage these files into the hub's configured script directory and invoke them
through its settings request/response pipeline. Script creation does not resolve
the existing config/ISA/plugin issues: the hub command mappings need valid YAML,
`GET_SAMPLE_RATE` must take no value argument in the ISA, and both meter getters
must index their arrays with the computed zero-based index. See the parent
[README](../README.md) for those integration prerequisites. Neither builds nor
runtime verification are part of this source update.
