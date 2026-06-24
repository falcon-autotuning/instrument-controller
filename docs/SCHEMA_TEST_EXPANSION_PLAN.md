# Schema Test Expansion Plan

## Goal

Expand the integration coverage in [`data-retrieval.cpp`](/home/zdm2/Documents/github/FAlCon/instrument-controller/tests/instrument-control/data-retrieval.cpp) so the Teal/Lua scripts generated from the measurement schema library can be validated systematically.

This plan assumes:

- there are 20 schema files under [`schemas/scripts`](/home/zdm2/Documents/github/FAlCon/falcon-measurement-lib/schemas/scripts)
- 4 schema-backed scripts/tests already exist
- the remaining work should stay inside the current `instrument-controller` test harness

## Current Inventory

### All 20 schema files

1. `get_all_voltages.json`
2. `get_many_voltages.json`
3. `get_number_of_samples.json`
4. `get_sample_rate.json`
5. `get_slope.json`
6. `get_trigger_leader.json`
7. `get_voltage.json`
8. `measure_1D_buffered.json`
9. `measure_2D_buffered.json`
10. `measure_current.json`
11. `measure_get_set.json`
12. `measure_illumination.json`
13. `measure_leakage.json`
14. `ramp.json`
15. `set_many_voltages.json`
16. `set_number_of_samples.json`
17. `set_sample_rate.json`
18. `set_slope.json`
19. `set_trigger_leader.json`
20. `set_voltage.json`

### Already represented by current schema-driven test work

These 4 are the best fit for “already created”:

1. `set_voltage`
2. `measure_get_set`
3. `measure_1D_buffered`
4. `measure_2D_buffered`

### Also present, but not part of the 20-schema set

These look like older/custom measurement scripts:

1. `1D Gaussian Noise.tl`
2. `1D Voltage Sweep Current.tl`
3. `2D Voltage Sweep Current.tl`

They are still useful as reference implementations, but they should not be the model for scaling all 20 schema-backed tests.

## Recommendation On Restructuring

Yes, I recommend restructuring [`data-retrieval.cpp`](/home/zdm2/Documents/github/FAlCon/instrument-controller/tests/instrument-control/data-retrieval.cpp) before adding many more tests.

Not a full rewrite. A light refactor is enough.

### Why restructure now

The current file already combines:

- test harness setup and hub lifecycle
- Teal compilation
- request construction
- protocol-specific assertions
- schema-specific expectations

That is manageable for 4 tests, but awkward for 20 because:

- getter/setter tests repeat the same port lookup patterns
- response assertions repeat the same shape checks
- script compilation is currently hard-coded one file at a time
- the file will become harder to scan than the schemas themselves

### What I would not do yet

- I would not split into many files immediately
- I would not introduce a large parameterized gtest matrix yet
- I would not move this into production helper libraries yet

A small in-file reorganization is enough for the next phase.

## Suggested Structure For `data-retrieval.cpp`

Keep one file for now, but organize it into sections.

### Section 1: Fixture and environment

Keep:

- hub setup/teardown
- per-test temp directory isolation
- config generation

Add:

- `CompileMeasurementScripts()` helper that compiles a list of schema-backed `.tl` files instead of one `CompileTeal(...)` call per script

### Section 2: Port lookup helpers

Add small helpers such as:

- `LookupVoltageSetter("P1")`
- `LookupVoltageGetter("O1")`
- `LookupStreamGetter("O1")`

This will remove repeated `request_knob(...)` / `request_meter(...)` blocks from every test.

### Section 3: Request builders by schema family

Group helpers into 4 families:

1. Setter-only
2. Getter-only
3. Getter + setter
4. Buffered sweep

Examples:

- `MakeSetterRequest(...)`
- `MakeGetterRequest(...)`
- `MakeMeasureGetSetRequest(...)`
- `MakeBuffered1DRequest(...)`
- `MakeBuffered2DRequest(...)`

### Section 4: Response assertion helpers

Add reusable assertions such as:

- `ExpectSingleScalarArray(...)`
- `ExpectArrayMetadata(...)`
- `ExpectArraySize(...)`
- `ExpectUnits(...)`
- `ExpectInstrumentType(...)`

That will make each test read like schema intent instead of protocol plumbing.

### Section 5: Tests grouped by schema category

Recommended ordering:

1. Setters
2. Getters
3. Combined get/set
4. Buffered measurements
5. Multi-port operations
6. Special/unsupported schemas

## Recommended Schema Grouping

This is the best way to add the remaining tests incrementally.

### Group A: Simple setter schemas

These are the easiest to add first because they usually return a single synthetic response and can reuse the `SetVoltage` pattern.

1. `set_sample_rate`
2. `set_number_of_samples`
3. `set_slope`
4. `set_trigger_leader`
5. `set_many_voltages`
6. `ramp`

### Group B: Simple getter schemas

These usually return one scalar or a small response from a single getter.

1. `get_voltage`
2. `get_sample_rate`
3. `get_number_of_samples`
4. `get_slope`
5. `get_trigger_leader`

### Group C: Multi-target getter/setter schemas

These need slightly richer request builders and assertions.

1. `get_many_voltages`
2. `get_all_voltages`
3. `set_many_voltages`

### Group D: Measurement schemas

These are more stateful and should come after the simpler protocol coverage is stable.

1. `measure_current`
2. `measure_illumination`
3. `measure_leakage`
4. `measure_get_set`
5. `measure_1D_buffered`
6. `measure_2D_buffered`

## Practical Expansion Order

I’d add them in this order:

1. `set_sample_rate`
2. `get_sample_rate`
3. `set_number_of_samples`
4. `get_number_of_samples`
5. `set_trigger_leader`
6. `get_trigger_leader`
7. `set_slope`
8. `get_slope`
9. `get_voltage`
10. `set_many_voltages`
11. `get_many_voltages`
12. `get_all_voltages`
13. `ramp`
14. `measure_current`
15. `measure_illumination`
16. `measure_leakage`

Reason:

- the first 8 let you reuse one simple “property set/get on one instrument channel” pattern
- those tests will force the reusable helper layer into shape
- once that exists, the multi-port and buffered tests become easier to write and review

## What A Good Test Template Looks Like

For each schema, define four things.

### 1. Schema contract

Document in a short comment:

- required ports
- expected globals passed to Lua
- expected response shape

### 2. Minimal Teal script

Keep each script narrowly aligned to the schema:

- use the exact schema field names
- avoid embedding extra behavior
- prefer one obvious side effect and one obvious return shape

### 3. Request builder

Each test should build the minimal valid request that exercises the schema.

### 4. Response assertions

Check only:

- connection
- instrument type
- units
- array count
- data length
- representative values

Avoid over-asserting incidental fields unless the schema explicitly depends on them.

## Specific Note For `set_sample_rate`

The attached [`set_sample_rate.json`](/home/zdm2/Documents/github/FAlCon/falcon-measurement-lib/schemas/scripts/set_sample_rate.json) is a good next candidate because it is small and clear.

Schema fields:

- `getter`
- `sampleRate`

This suggests a direct pattern:

1. Teal script:
   - accept `ctx`, `getter`, `sampleRate`
   - call the mock multimeter sample-rate setter for the target channel
   - return a simple success value or rely on the hub synthetic `MeasurementResponse`

2. GTest:
   - request the stream-capable getter for `O1`
   - build a minimal request named `set_sample_rate`
   - assert returned connection/type/units/size in the same style as `SetVoltage`

That makes it a very good first “post-restructure” schema.

## Proposed Refactor Phases

### Phase 1: Reorganize helpers in-place

Change only [`data-retrieval.cpp`](/home/zdm2/Documents/github/FAlCon/instrument-controller/tests/instrument-control/data-retrieval.cpp):

1. Add grouped helper sections
2. Add `CompileMeasurementScripts(...)`
3. Add port lookup helpers
4. Add response assertion helpers
5. Leave existing test names intact

### Phase 2: Normalize the existing 4 schema-backed tests

Update:

1. `SetVoltage`
2. `Gaussian1DMeasureGetSet`
3. `VoltageSweepCurrent`
4. `VoltageSweepCurrent2D`

so they all use the same helper style.

### Phase 3: Add simple property tests in pairs

For each property family:

1. add `set_*`
2. add matching `get_*`

This gives immediate confidence that the mocks, hub, and script mapping all agree.

### Phase 4: Add multi-target tests

Add:

1. `set_many_voltages`
2. `get_many_voltages`
3. `get_all_voltages`

### Phase 5: Fill in special measurement schemas

Add:

1. `measure_current`
2. `measure_illumination`
3. `measure_leakage`

## Answer To “Should I restructure first?”

Yes, but lightly.

Recommended answer:

- restructure the current file before adding many more tests
- keep it as one file for now
- extract shared helpers and group tests by schema family

That gives you the best tradeoff:

- better maintainability
- low churn
- no need to redesign the whole harness yet

## Suggested Next Step

1. Do the light `data-retrieval.cpp` helper refactor first
2. Add `set_sample_rate.tl`
3. Add `TEST_F(DataRetrievalTest, SetSampleRate)`

That will validate the new pattern on a schema that is small, representative, and easy to debug.
