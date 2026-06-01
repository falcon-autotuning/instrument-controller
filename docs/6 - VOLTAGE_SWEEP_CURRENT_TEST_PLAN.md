# Voltage Sweep / Current Record Test Plan

**Context**: Add a new `VoltageSweepCurrent` integration test to `instrument-controller`
that performs a 1D voltage sweep on P1 and records current at O2 (Meter1 channel 2),
returning and asserting on the resulting DataBuffer labelled array.

All port construction is manual — no `request_port_payload()` calls — so the four
blocking issues described in `5 - PORT_PAYLOAD_REFACTOR_NOTES.md` do not apply here.

---

## Pre-Confirmed: No Blocking Issues

| Concern | Status |
|---|---|
| `O2` in wiremap → Meter1 channel 2 | ✅ Already present in `2-dot-1-chargesensor-wiremap.yml` |
| `analog2_stream` in multimeter config | ✅ Already declared in `multimeter-config.yml` |
| Mock multimeter per-channel data files | ✅ Supported via semicolon-separated `MOCK_MULTIMETER_DATA_FILE` |
| `tests/test-data/linear-1d.txt` as current data | ✅ Already exists |
| Correct ammeter spelling | ✅ `InstrumentTypes::AMNMETER` (consistent codebase convention) |

---

## Open Decisions

Before implementing, confirm the following:

### 1. File organisation
Add `VoltageSweepCurrent` as a second `TEST_F` in the existing
`tests/data-retrieval-1D/data-retrieval.cpp`, or create a new directory
`tests/voltage-sweep-current/` mirroring the existing 1D structure?

A separate directory keeps scenarios cleanly isolated as the test suite grows; the same
file is simpler for now.

### 2. Current data source
Which file should mock channel 2 (O2) serve?

| Option | File | Notes |
|--------|------|-------|
| A | `tests/test-data/linear-1d.txt` | Already exists; realistic linear I-V shape |
| B | `tests/test-data/gaussian-1d.txt` | Zero new files; identical shape to Gaussian1D |
| C | New `gen_current_data.cpp` generator | Physics-accurate nA Gaussian; adds a build step |

**Recommended**: Option A — already exists, no new build step, gives a physically
distinct response shape from `Gaussian1D`.

### 3. Current units
`SymbolUnit::NanoAmpere()` / `PicoAmpere()` / `Ampere()`? Affects only the
assertion; the mock returns raw doubles regardless.

### 4. Teal script reuse
The existing `1D Gaussian Noise.tl` already accepts arbitrary getter channels and can
serve the current sweep without modification. A dedicated
`1D Voltage Sweep Current.tl` is cleaner long-term but not required.

---

## Implementation Steps

### Phase 1 — Data layer

Update `SetUp()` in
`tests/data-retrieval-1D/data-retrieval.cpp` — change the `MOCK_MULTIMETER_DATA_FILE`
`setenv` call from one path to two semicolon-separated paths:

```cpp
// Before
setenv("MOCK_MULTIMETER_DATA_FILE", TEST_DATA_FILE.c_str(), 1);

// After
std::string multimeter_data_files =
    TEST_DATA_FILE.string() + ";" +
    (TEST_DATA_DIR_PATH / "linear-1d.txt").string();
setenv("MOCK_MULTIMETER_DATA_FILE", multimeter_data_files.c_str(), 1);
```

Channel 1 (O1) continues to receive Gaussian data; channel 2 (O2) receives linear
data. The existing `Gaussian1D` test is unaffected.

### Phase 2 — Teal measurement script *(conditional on Decision 4)*

**If a new script is preferred**, write
`tests/data-retrieval-1D/measurement-scripts/1D Voltage Sweep Current.tl`.
The body is structurally identical to `1D Gaussian Noise.tl`:

```lua
local function VoltageSweepCurrent(
  ctx: RuntimeContext,
  getters: {InstrumentTarget},
  sweepVoltages: {number},
  setters: {InstrumentTarget}
): string
  for _, voltage in ipairs(sweepVoltages) do
    for _, setter in ipairs(setters) do
      source:setVoltage(setter.id, setter.channel, voltage)
    end
    for _, getter in ipairs(getters) do
      multimeter:getDatapoint(getter.id, getter.channel)
    end
  end
  return ""
end
return { main = VoltageSweepCurrent }
```

Add the corresponding `CompileTeal(...)` call in `SetUp()`:

```cpp
CompileTeal(
    (MEASUREMENT_SCRIPTS_DIR / "1D Voltage Sweep Current.tl").string(),
    (LUA_SCRIPTS_DIR / "1D Voltage Sweep Current.lua").string());
```

**If reusing the existing script**, pass O2 as the getter in the `MeasurementRequest`
and keep the script name `"1D Gaussian Noise"` — no `SetUp()` change needed beyond
Phase 1.

### Phase 3 — C++ test case

Add `TEST_F(DataRetrievalTest, VoltageSweepCurrent)` to `data-retrieval.cpp`.
All port construction is **manual** — no `request_port_payload()`:

```cpp
TEST_F(DataRetrievalTest, VoltageSweepCurrent)
{
  const int NUM_POINTS = 100;
  const double START_TIME_SECONDS = 0.0;
  const double MAX_TIME_SECONDS = 1.0;
  const char *SWEEP_NAME = "P1";
  const char *GETTER_NAME = "O2";
  const double MIN_SWEEP_VOLTAGE = 0.0;
  const double MAX_SWEEP_VOLTAGE = 0.5;

  ConfigSP config = request_config(TIMEOUT_MS);
  ASSERT_NE(config, nullptr) << "Failed to get config from request_config";

  // TODO(port_payload): Manually constructed — replace with request_port_payload()
  // once PORT_PAYLOAD serialisation is stable (see 5 - PORT_PAYLOAD_REFACTOR_NOTES.md).
  InstrumentPortSP currentMeter = InstrumentPort::Meter(
      "analog2_stream", Connection::Ohmic(GETTER_NAME),
      InstrumentTypes::AMNMETER, SymbolUnit::NanoAmpere(),
      "Mock current meter");
  PortsSP getters =
      std::make_shared<Ports>(std::vector<InstrumentPortSP>{currentMeter});

  InstrumentPortSP voltageKnob = InstrumentPort::Knob(
      "analog4_voltage", Connection::PlungerGate(SWEEP_NAME),
      InstrumentTypes::DC_VOLTAGE_SOURCE, SymbolUnit::Volt(),
      "Voltage sweep knob");
  InstrumentPortSP clock = InstrumentPort::ExecutionClock();

  MapSP<InstrumentPort, PortTransform> transforms =
      std::make_shared<Map<InstrumentPort, PortTransform>>(
          std::vector<std::pair<InstrumentPortSP, PortTransformSP>>{
              {voltageKnob,
               PortTransform::IdentityTransform(voltageKnob)}});
  LabelledDomainSP time_domain = LabelledDomain::from_port(
      std::pair<double, double>{START_TIME_SECONDS, MAX_TIME_SECONDS}, clock);
  LabelledDomainSP sweepDomain = LabelledDomain::from_port(
      std::pair<double, double>{MIN_SWEEP_VOLTAGE, MAX_SWEEP_VOLTAGE},
      voltageKnob);
  CoupledLabelledDomainSP coupledDomain =
      std::make_shared<CoupledLabelledDomain>(
          std::vector<LabelledDomainSP>{sweepDomain});
  MapSP<std::string, bool> increasing =
      std::make_shared<Map<std::string, bool>>(
          std::vector<std::pair<std::string, bool>>{
              {voltageKnob->default_name(), true}});

  auto waveform = Waveform::CartesianIdentityWaveform1D(
      NUM_POINTS, coupledDomain, increasing);
  ListSP<Waveform> waveforms =
      std::make_shared<List<Waveform>>(std::vector<WaveformSP>{waveform});

  MeasurementRequestSP request = std::make_shared<MeasurementRequest>(
      "1D voltage sweep recording current at O2",
      "1D Voltage Sweep Current",   // or "1D Gaussian Noise" if reusing script
      waveforms, getters, transforms, time_domain);
  auto resp = request_measurement(request, TIMEOUT_MS);

  EXPECT_TRUE(resp->arrays()->is_measured_arrays())
      << "The arrays were not measured arrays";
  EXPECT_EQ(resp->arrays()->size(), 1)
      << "Expected exactly one measured array";
  auto labelledArray = resp->arrays()->arrays()[0];
  EXPECT_EQ(labelledArray->instrument_type(), InstrumentTypes::AMNMETER)
      << "Expected instrument type to be AMNMETER, but got "
      << labelledArray->instrument_type() << " instead";
  EXPECT_EQ(*labelledArray->units(), *SymbolUnit::NanoAmpere())
      << "Expected units to be nA, but got " << labelledArray->units()
      << " instead";
  EXPECT_EQ(labelledArray->size(), NUM_POINTS)
      << "Expected 100 data points, but got " << labelledArray->size()
      << " instead";
}
```

### Phase 4 — Build system *(minimal)*

Update `tests/CMakeLists.txt` **only** if a new `.cpp` file is created (append to
`TEST_SOURCES`). No new mock plugin binary is needed.

```cmake
set(TEST_SOURCES
  data-retrieval-1D/data-retrieval.cpp
  voltage-sweep-current/voltage-sweep-current.cpp   # add only if new file
)
```

---

## Related Files

| File | Change Required |
|------|-----------------|
| `tests/data-retrieval-1D/data-retrieval.cpp` | `SetUp()` env var update + new `TEST_F` |
| `tests/data-retrieval-1D/measurement-scripts/1D Gaussian Noise.tl` | Reuse as-is (or template for new script) |
| `tests/test-data/linear-1d.txt` | Used as channel-2 current data — no change |
| `tests/instrument-plugins/mock-multimeter.c` | **No changes needed** |
| `tests/data-retrieval-1D/multimeter-config.yml` | **No changes needed** |
| `tests/data-retrieval-1D/2-dot-1-chargesensor-wiremap.yml` | **No changes needed** |
| `tests/CMakeLists.txt` | Only if new `.cpp` is added |

---

## Verification

```bash
# Build
cmake --build build/linux-clang-release --target integration_tests

# Run new test only
./build/linux-clang-release/tests/integration_tests \
    --gtest_filter=DataRetrievalTest.VoltageSweepCurrent

# Run full suite to confirm Gaussian1D still passes
./build/linux-clang-release/tests/integration_tests \
    --gtest_filter=DataRetrievalTest.*
```

Expected: all `EXPECT_*` assertions pass; `labelledArray->size()` == 100.
