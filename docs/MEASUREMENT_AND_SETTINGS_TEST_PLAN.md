# Measurement and settings integration test checklist

Goal: establish two independent controller → NATS → hub → ISS → mock plugin
pipelines, each returning its own Falcon response type. Reuse process helpers
and mock assets, but give each fixture its own config, scripts, and runtime state.

Naming: keep the measurement script `measure_get_set.lua`; name its test
`measure_set_get`. The current `DataRetrievalTest.measure_set_get` lives in `data-retrieval.cpp`
and exercises the hub pipeline using the existing fixture.

## 1. Agree on the Lua contracts

- [ ] Use `falcon-measurement-lib/schemas/scripts/measure_get_set.json`,
  `get_voltage.json`, and `set_voltage.json` as the script contract references.
- [ ] Resolve schema/hub differences before writing request builders. Currently
  the measurement schema describes a voltage map, `sampleRate`, and `numPoints`,
  while the hub's `encodeGetSet` sends positional `setVoltages`, `getters`, and
  `setters` arrays. Agree on one contract and update the affected components.
- [ ] Confirm result units: the mock multimeter returns volts; the measurement
  schema currently describes nA. Make the contract and assertions agree.
- [ ] Write plain Lua with direct `ctx:call` calls and
  `instrument_call_stack.new(...)`. Use native `instrument-target` accessors
  and the libraries' deserializers when the boundary supplies serialized values.
  Use `instrument-domain` bindings wherever a request includes domains; do not
  introduce domains into a scalar script that does not need them.
- [ ] Remove Teal compilation and generated instrument-wrapper dependencies
  from these test paths. Preserve write-before-read ordering; use
  `ctx:parallel` only for operations that may execute together.

## 2. Prepare separate hub configurations and fixtures

- [ ] Add `MeasurementPipelineTest` and `SettingsPipelineTest`, preferably in
  separate test source files, with shared helpers only for process lifecycle,
  paths, readiness, and cleanup.
- [ ] Create separate measurement/settings hub-config templates validated
  against the schema shipped with the selected hub release. The current hub
  checkout's reference is `schemas/hub.schema.json`; do not copy the old flat
  `test_data/test-config.yaml` layout.
- [ ] Populate wiremap, quantum-dot config, initial voltage state, database
  paths/owner, working directory, script directory, and ISS port/autostart.
- [ ] Measurement config: load the source and multimeter test ISAs and plugins,
  use `voltage_source`/`voltmeter` types, and map a source channel and meter
  channel to distinct logical connections.
- [ ] Settings config: load the source test ISA/plugin and declare a
  `source_voltage` characteristic with read/write mappings to `GET_VOLTAGE`
  and `SET_VOLTAGE`. Verify the hub publishes a readable/writable setting port.
- [ ] Give each fixture its own script directory. Initially expose only
  `measure_get_set.lua` to the measurement fixture and `get_voltage.lua` /
  `set_voltage.lua` to the settings fixture. Add any required support scripts
  explicitly, rather than enabling all sweep scripts and their prerequisites.
- [ ] Generate NATS host/port and `NATS_URL` from the same fixture values.
  Isolate ISS runtime files, mock data, and database files per run; distinguish
  the SQLite catalog file from its containing directory.
- [ ] Add bounded readiness waits, surface early hub/ISS exits with log paths,
  and stop owned processes and restore environment variables during teardown.
  Allocate separate ports or serialize tests that share resources.

## 3. Measurement request/response pipeline

- [ ] Implement `measure_get_set.lua` using direct `SET_VOLTAGE` followed by
  `GET_DATAPOINT`, with the agreed arguments and native target bindings.
- [ ] Add `MeasurementPipelineTest.measure_set_get` using
  `falcon::routine::request_measurement` with a real `MeasurementRequest`.
  Use hub-published knob/meter metadata and a single-point voltage request.
- [ ] Set the source to 0.123 V and give the mock multimeter a deterministic
  fixture containing 0.375 V. These plugins are independent; do not expect the
  meter to echo the source voltage.
- [ ] Verify source write execution through readback or command tracing, then
  assert a non-null `MeasurementResponse`, one measured array, the getter's
  logical connection, volts, and the expected single numeric sample.
- [ ] Confirm the request traverses the measurement handler and returns through
  the measurement response/data transport. A direct ISS result alone is not
  the acceptance criterion.

## 4. Settings request/response pipeline

- [ ] Implement `set_voltage.lua(ctx, setter, setVoltage)` and
  `get_voltage.lua(ctx, getter)` using direct calls to the source ISA.
  Confirm argument names/order and return handling against the settings router.
- [ ] Add `SettingsPipelineTest.get_voltage`: send a getter-only
  `SettingRequest` through `falcon::routine::request_setting` and check the
  source's initial voltage in the returned `SettingResponse` getter map.
- [ ] Add `SettingsPipelineTest.set_voltage`: send a setter-only
  `SettingRequest` for 0.123 V, check the response, then send a separate
  getter-only request and assert 0.123 V with the mock float tolerance.
- [ ] Use the hub-published `source_voltage` setting port and voltage units.
  Do not encode settings as measurement waveforms or expect measured arrays.
- [ ] Confirm routing through the settings handler and settings response
  transport. Do not require a setter-only response to echo a measured value.

## 5. Completion checks

- [ ] Register distinct CTest labels/filters for measurement and settings tests.
- [ ] Each new pipeline passes alone and both pass sequentially without stale
  daemons, shared mock state, or port conflicts.
- [ ] Retire deprecated tests that send settings through `MeasurementRequest`.
- [ ] Update the test README to document plain Lua, both configs, manual run
  commands, and the distinction between ISS smoke tests and hub integration tests.
