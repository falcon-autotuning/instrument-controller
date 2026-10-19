# Measurement script integration tests

The Lua examples previously under the hub's
`test_data/measurement-scripts/` now live here alongside their Teal sources:

- `measurement-scripts/`: scalar set/read, 1D sweep, and 2D raster scripts.
- `settings-scripts/`: settings scripts, including source set/read helpers.
- `instrument-libs/`: `source` and `multimeter` adapters for the mock ISAs.
- `lua-utils/`: sweep interpolation helpers. Import `sweep_math`, since
  `require("math")` resolves to Lua's built-in math library.

Every moved Lua file has a corresponding `.tl` source. The scripts use native
`InstrumentTarget` and `InstrumentDomain` accessor methods, and positional
voltage/domain arrays. `measure_get_set` takes `ctx, setVoltages, getters,
setters`; acquisition settings are separate from those arguments. The sweep
examples issue scalar reads at each point; they do not implement a hardware
buffered sweep.

## Run the scalar test

After building with your usual preset, run from the controller repository:

```sh
. build/linux-clang-release/env.sh
export PATH="$VCPKG_INSTALLED_DIR/$VCPKG_TRIPLET/bin:$PATH"
export LD_LIBRARY_PATH="$VCPKG_INSTALLED_DIR/$VCPKG_TRIPLET/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
ctest --preset linux-clang-release \
  -R '^MeasurementScriptTest\.measure_set_get$' --output-on-failure -V
```

`measurement_script_tests` starts its own ISS daemon with a private runtime
directory and an available RPC port. It expands the existing source/multimeter
ISA templates into its run directory and loads `mock_voltage_source` and
`mock_multimeter` from the test build. The test executes the checked-in Lua
directly; Teal compilation is not part of this smoke test.

The test checks that `measure_get_set.lua` sets Source1 analog channel 1 to
0.123 V, then reads Meter1 analog channel 2 as 0.375 V. It independently reads
the source back through `get_many_voltages.lua`. The mock meter reads a fixed
data file and is not electrically coupled to the source, so its value should
not equal the source setting.

The test stops its own daemon and workers and restores its environment. Logs,
expanded ISAs, and data remain under `tests/test-runs/measure_set_get_*` for
inspection. Existing controller-to-hub tests continue to use their own fixture.
