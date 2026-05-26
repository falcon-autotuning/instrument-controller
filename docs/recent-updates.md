# Recent Updates

## 2026-05-25

### Bug Fix — `mock-multimeter.c`: Array out-of-bounds crash on 9th `GET_DATAPOINT` call

**File:** `tests/instrument-plugins/mock-multimeter.c`

The Meter1 worker would silently crash (SIGSEGV) after 8 successful `GET_DATAPOINT` calls,
causing all subsequent ISS commands to time out. Three related bugs combined to cause this:

1. **Array size off-by-one** — All five global state arrays were declared with size
   `MAX_CHANNEL - MIN_CHANNEL` (= 7 elements, indices 0–6). However, `getArrayIndex(c)`
   returns `c - MIN_CHANNEL`, meaning the highest valid channel (8) maps to index 7, which
   was out of bounds. Fixed by changing all array sizes to `MAX_CHANNEL - MIN_CHANNEL + 1`
   (8 elements):
   - `g_data_file_path`
   - `g_num_bins`
   - `g_data_buffer`
   - `g_data_count`
   - `g_current_index`

2. **Wrong data access dimension in `handle_datapoint()`** — The expression
   `*g_data_buffer[g_current_index[index]]` was using the per-channel read counter as an
   index into the *outer* pointer array rather than into the channel's own data buffer.
   After 7 calls, the counter exceeded the array bounds. Fixed to
   `g_data_buffer[index][g_current_index[index]]`.

3. **Same wrong data access in `handle_stream()`** — Same fix applied.

4. **Loop bounds in `plugin_initialize`, `plugin_shutdown`, `handle_reset`** — Loops were
   iterating `MAX_CHANNEL - MIN_CHANNEL` times (7), missing the 8th channel slot. Updated
   to `MAX_CHANNEL - MIN_CHANNEL + 1`.

---

### Bug Fix — `mock-multimeter.c` / `mock-voltage-source.c`: Parameter index never written

**Files:** `tests/instrument-plugins/mock-multimeter.c`,
`tests/instrument-plugins/mock-voltage-source.c`

`get_param_index()` accepted `int out_index` by value, so the found index was never
returned to the caller. All parameter lookups silently failed and commands returned
`MISSING_PARAMETERS_ERROR`. Fixed the signature to `int *out_index` and all call sites
updated to pass the address (e.g., `&channel_idx`).

---

### Bug Fix — `mock-multimeter.c` / `mock-voltage-source.c`: ISS numeric type compatibility

**Files:** `tests/instrument-plugins/mock-multimeter.c`,
`tests/instrument-plugins/mock-voltage-source.c`

The ISS (instrument-script-server) encodes all Lua integers as `PARAM_TYPE_INT64` and all
Lua numbers as `PARAM_TYPE_DOUBLE`. The plugins were checking for `PARAM_TYPE_INT32`
exactly, causing every incoming command to fail with `INVALID_PARAMETER_TYPE_ERROR`.

Changes:
- `check_param_type()` now accepts `INT64` and `DOUBLE` wherever `INT32` is expected, and
  `DOUBLE` wherever `FLOAT` is expected.
- Added `read_int_param()` helper to safely read INT32/INT64/DOUBLE params as `int`.
- Added `read_float_param()` helper (voltage-source only) to read FLOAT/DOUBLE params as
  `float`.

---

### Fix — Teal/Lua instrument libraries: ISS call format

**Files:** `tests/teal/multimeter.tl`, `tests/teal/source.tl`,
`tests/instrument-lua-libs/multimeter.lua`, `tests/instrument-lua-libs/source.lua`

The generated Teal/Lua libraries used the legacy positional call format
`context:call("id:channel.VERB", value)`. The ISS expects named parameters passed as a
table. Updated all `context:call()` invocations to use the named-parameter table form:

```lua
-- Before
context:call(id .. ':' .. tostring(channel) .. '.SET_BINS', bins)

-- After
context:call(id .. '.SET_BINS', { analog = channel, bins = bins })
```

Also removed the `local` qualifier from the instrument table declarations (`Mock5Meter1`,
`Mock1Source1`) so they are accessible as globals by measurement scripts.

---

### New File — `1D Gaussian Noise.tl` measurement script

**File:** `tests/data-retrieval-1D/measurement-scripts/1D Gaussian Noise.tl`
**Compiled output:** `tests/lua/1D Gaussian Noise.lua`

Added a new Teal measurement script implementing a non-buffered 1D voltage sweep. For each
voltage in `sweepVoltages` it calls `source:setVoltage()` on all setters, then
`multimeter:getDatapoint()` on all getters. The compiled Lua output is generated
automatically by the test setup step in `data-retrieval.cpp`.

---

### Fix — Instrument config names to match ISS worker names

**Files:** `tests/data-retrieval-1D/multimeter-config.yml`,
`tests/data-retrieval-1D/source-config.yml`

Renamed instrument names to match what the ISS registers as the worker name:
- `MockInstrument1` → `Meter1`
- `MockInstrument2` → `Source1`

Mismatched names caused all IPC messages to be routed to non-existent workers.

---

### Fix — Wire map format update

**File:** `tests/data-retrieval-1D/2-dot-1-chargesensor-wiremap.yml`

Rewrote the wire map from the legacy flat format:
```yaml
Source1.1: S1
Meter1.1: O2
```

To the structured format expected by the hub:
```yaml
wiremap:
  - name: S1
    instrument:
      name: Source1
      channel_name: analog
      index: 1
  - name: O2
    instrument:
      name: Meter1
      channel_name: analog
      index: 1
```

---

### Update — Port versions

**Files:** `ports/falcon-instrument-hub/portfile.cmake`,
`ports/falcon-instrument-hub/vcpkg.json`,
`ports/falcon-measurement-lib/portfile.cmake`,
`ports/falcon-measurement-lib/vcpkg.json`,
`ports/instrument-script-server/portfile.cmake`,
`ports/instrument-script-server/vcpkg.json`,
`ports/teal-api-gen/portfile.cmake`,
`ports/teal-api-gen/vcpkg.json`

Updated vcpkg port definitions to track latest HEAD commits for all four dependency ports.

---

### Test result

`DataRetrievalTest.Gaussian1D` — **PASSED** (2.15 s) after applying the above fixes.
