# MeasureResponse Hang Fix — `DataRetrievalTest.Gaussian1D`

**Date:** 2026-05-30  
**Test:** `instrument-controller/tests/data-retrieval-1D/data-retrieval.cpp` → `DataRetrievalTest.Gaussian1D`  
**Result:** ✅ PASSED (5.99 s)

---

## Root Cause

The test hung indefinitely inside `request_measurement()` because the hub published a `FALCON.MEASURE_RESPONSE` NATS message with empty `stream` and `channel` fields, causing the C++ client to call `js_PullSubscribe("", "", ...)` — a NATS JetStream subscribe with an empty stream name that blocks in the cnats library indefinitely.

### Full call chain

```
DataRetrievalTest::Gaussian1D
  └── request_measurement(request, 5000ms)           ← falcon-routine/hub.cpp
        └── comms.subscribe_measure_response(...)     ← succeeds in ~70 ms
              [hub publishes api.MeasureResponse{Stream:"", Channel:"", Response:respJSON}]
        └── comms.pull_measurement_data("", "", 1)   ← HANGS HERE
              └── hub_.jetstream_pull("", "", 1)
                    └── js_PullSubscribe(js_, "", "", NULL, NULL, NULL)
                          ← cnats blocks indefinitely on empty stream name
```

### Why the 5 s timeout did not fire

The 5 s timeout in `subscribe_measure_response` guards the **NATS subscribe wait** — the point at which the C++ side waits for the hub to publish to `FALCON.MEASURE_RESPONSE`. That fires and completes successfully within ~70 ms (ISS finished serialising 200 results at `21:32:55.475`). The hang occurs **after** that, inside `pull_measurement_data`, which has no timeout of its own — `js_PullSubscribe` with an empty stream name never returns an error, it just blocks.

### Why stream and channel were empty

`falcon-instrument-hub/runtime/internal/handlers/measure_command_handler.go` assembled the response struct as:

```go
measureResp := api.MeasureResponse{
    Response:  respJSON,        // full MeasurementResponse JSON ✓
    Timestamp: time.Now().UnixMicro(),
    Hash:      cmd.Hash,
    // Stream and Channel were never set → both ""
}
```

The `Response` field carries the full `MeasurementResponse` JSON, but the C++ side in `falcon-routine/hub.cpp` was ignoring it and calling `pull_measurement_data(resp.stream, resp.channel, 1)` unconditionally, expecting data to be in JetStream. The JetStream path was never implemented on the hub side for this code path.

### Design context

Every other response type in falcon-routine (`request_device_state`, `request_config`) uses the embedded `response` string from the NATS message directly:

```cpp
auto resp = comms.subscribe_state_response(timeout_ms, value);
return VoltageStatesResponse::from_json_string<...>(resp.response);  // ← direct
```

`request_measurement` was the sole outlier that went via JetStream.

---

## Files Changed

### 1. `falcon/comms/include/falcon-comms/commands_definitions.hpp` *(new override file)*

**Path:** `falcon/comms/include/falcon-comms/commands_definitions.hpp`

Added a `response` field to the `MeasureResponse` C++ struct so the inline JSON carried in the NATS message can be surfaced to callers:

```cpp
// Before (upstream v1.0.10)
struct FALCON_COMMS_API MeasureResponse : public CommandBase {
  static constexpr const char *NAME = "MEASURE_RESPONSE";
  std::string stream;
  long long timestamp = 0;
  std::string channel;
  // ... ctors/dtors/methods
};

// After (Phase 4b override)
struct FALCON_COMMS_API MeasureResponse : public CommandBase {
  static constexpr const char *NAME = "MEASURE_RESPONSE";
  std::string stream;
  long long timestamp = 0;
  std::string channel;
  std::string response;  // ← added: inline measurement JSON when stream is empty
  // ... ctors/dtors/methods
};
```

This mirrors the pattern already used by `StateResponse`, `DeviceConfigResponse`, and the Go `api.MeasureResponse.Response` field.

---

### 2. `falcon/comms/src/commands.cpp` *(new override file)*

**Path:** `falcon/comms/src/commands.cpp`

Extended `MeasureResponse::to_json` and `MeasureResponse::from_json` to serialise/deserialise the new `response` field:

```cpp
// to_json
nlohmann::json MeasureResponse::to_json() const {
  nlohmann::json json;
  json["channel"] = channel;
  json["stream"]  = stream;
  json["response"] = response;   // ← added
  return json;
}

// from_json
MeasureResponse MeasureResponse::from_json(const nlohmann::json &j) {
  MeasureResponse obj;
  if (j.contains("channel"))  j.at("channel").get_to(obj.channel);
  if (j.contains("stream"))   j.at("stream").get_to(obj.stream);
  if (j.contains("response")) j.at("response").get_to(obj.response);  // ← added
  return obj;
}
```

---

### 3. `falcon/comms/src/hub_override/hub.cpp` *(new override file)*

**Path:** `falcon/comms/src/hub_override/hub.cpp`

Changed `request_measurement` in `falcon-routine` to match the same pattern used by every other request helper — use `resp.response` directly when `stream` is empty, with a fallback to the JetStream pull path for future use:

```cpp
// Before (upstream v1.0.5)
falcon_core::communications::messages::MeasurementResponseSP
request_measurement(..., int timeout_ms) {
  falcon::comms::RoutineComms comms;
  auto resp = comms.subscribe_measure_response(json_req, timeout_ms, value);
  auto outs = comms.pull_measurement_data(resp.stream, resp.channel, 1);  // ← hangs
  return MeasurementResponse::from_json_string<...>(outs[0]);             // ← OOB if empty
}

// After (Phase 4b override)
falcon_core::communications::messages::MeasurementResponseSP
request_measurement(..., int timeout_ms) {
  falcon::comms::RoutineComms comms;
  auto resp = comms.subscribe_measure_response(json_req, timeout_ms, value);
  if (!resp.stream.empty()) {
    // JetStream path retained for future use when hub populates stream/channel
    auto outs = comms.pull_measurement_data(resp.stream, resp.channel, 1);
    return MeasurementResponse::from_json_string<...>(outs[0]);
  }
  // Inline path: hub embeds MeasurementResponse JSON directly in resp.response
  return MeasurementResponse::from_json_string<...>(resp.response);
}
```

---

### 4. `instrument-controller/ports/falcon-comms/portfile.cmake` *(modified)*

Added two new `file(COPY ...)` override injections so vcpkg uses the local `commands_definitions.hpp` and `commands.cpp` overrides when building `falcon-comms` from the upstream GitHub source:

```cmake
# Existing overrides (unchanged)
file(COPY "${WORKSPACE_ROOT}/falcon/comms/src/routine_comms.cpp"
     DESTINATION "${SOURCE_PATH}/src")
file(COPY "${WORKSPACE_ROOT}/falcon/comms/include/falcon-comms/routine_comms.hpp"
     DESTINATION "${SOURCE_PATH}/include/falcon-comms")

# New Phase 4b overrides
file(COPY "${WORKSPACE_ROOT}/falcon/comms/include/falcon-comms/commands_definitions.hpp"
     DESTINATION "${SOURCE_PATH}/include/falcon-comms")

file(COPY "${WORKSPACE_ROOT}/falcon/comms/src/commands.cpp"
     DESTINATION "${SOURCE_PATH}/src")
```

Each is guarded by `if(EXISTS ...)` so absent override files are silently skipped.

---

### 5. `instrument-controller/ports/falcon-comms/vcpkg.json` *(modified)*

Bumped `port-version` from `2` to `3` to force vcpkg to rebuild `falcon-comms` with the new portfile:

```json
// Before
"port-version": 2

// After
"port-version": 3
```

---

### 6. `instrument-controller/ports/falcon-routine/vcpkg.json` *(modified)*

Bumped `port-version` from `1` to `2` to force vcpkg to rebuild `falcon-routine` with the new `hub.cpp` override:

```json
// Before
"port-version": 1

// After
"port-version": 2
```

The `falcon-routine` portfile already contained the `hub_override/hub.cpp` injection hook from a previous session:

```cmake
set(FALCON_ROUTINE_HUB_OVERRIDE "${WORKSPACE_ROOT}/falcon/comms/src/hub_override/hub.cpp")
if(EXISTS "${FALCON_ROUTINE_HUB_OVERRIDE}")
    file(COPY "${FALCON_ROUTINE_HUB_OVERRIDE}" DESTINATION "${SOURCE_PATH}/src")
endif()
```

The port-version bump was the only change needed here — the hook was already in place.

---

### 7. `instrument-script-server/src/server/CommandHandlers.cpp` *(modified — diagnostic)*

Added two `LOG_INFO` checkpoints in `handle_measure` to confirm that the ISS Lua execution and HTTP response serialisation were completing before the hang:

```cpp
// After Lua script execution completes and results are collected:
LOG_INFO("SERVER", "MEASURE",
         "Lua script done. Serializing {} results into HTTP response",
         results.size());

// ... serialisation loop ...

// After all results are serialised into the HTTP response body:
LOG_INFO("SERVER", "MEASURE",
         "Serialization complete. Returning to HTTP handler to send response");
```

These confirmed in the test logs (`iss-daemon.log`) that ISS reached both checkpoints at `21:32:55.475` — within ~70 ms of the Lua script starting — ruling out ISS as the source of the hang.

---

### 8. `instrument-script-server/src/server/RuntimeContext.cpp` *(modified — diagnostic)*

Promoted two `LOG_DEBUG` calls to `LOG_INFO` in `call()` and `send_command()` so they appear at the default log level during test runs, giving per-command visibility into the IPC layer:

```cpp
// call() — function dispatch into Lua
LOG_INFO("LUA_CONTEXT", "CALL", "Calling function: {}", func_name);

// send_command() — synchronous IPC dispatch to worker process
LOG_INFO("LUA_CONTEXT", "SEND",
         "Sending command {}.{} (expects_response={})", instrument_id, verb,
         expects_response);
LOG_INFO("LUA_CONTEXT", "SEND",
         "Command {}.{} returned: success={} error='{}'",
         instrument_id, verb, resp.success,
         resp.success ? "" : resp.error_message);
```

These confirmed all 100 IPC commands (50 set-voltage + 50 get-stream per sweep step) were dispatched and responded to successfully, ruling out a stall in the IPC layer.

---

### 9. `instrument-script-server/src/server/InstrumentWorkerProxy.cpp` *(modified — diagnostic)*

Promoted `LOG_DEBUG` to `LOG_INFO` for command enqueue and response receipt in `execute()` and `execute_sync()`, giving per-command visibility from the worker proxy side:

```cpp
// execute() — async enqueue
LOG_INFO(instrument_name_, cmd.id, "Enqueueing command:  {} (sync={})",
         cmd.verb, cmd.sync_token.has_value());

// execute_sync() — blocking wait result
LOG_INFO(instrument_name_, "PROXY",
         "Response listener started");
```

Together with the `RuntimeContext.cpp` changes, these confirmed the full IPC round-trip for all 100 commands completed before the hang.

---

### 10. `instrument-controller/ports/instrument-script-server/vcpkg.json` *(modified — diagnostic)*

Bumped `port-version` from `12` to `13` to force vcpkg to rebuild ISS and pick up the new `LOG_INFO` checkpoints:

```json
// Before
"port-version": 12

// After
"port-version": 13
```

---

## Override File Layout

All local override files live under `falcon/comms/` at the workspace root, which the portfiles resolve via `get_filename_component(WORKSPACE_ROOT "${CURRENT_PORT_DIR}/../../.." ABSOLUTE)`:

```
falcon/
└── comms/
    ├── include/
    │   └── falcon-comms/
    │       ├── commands_definitions.hpp   ← adds response field to MeasureResponse
    │       └── routine_comms.hpp          ← (pre-existing, unchanged)
    └── src/
        ├── commands.cpp                   ← parses response in MeasureResponse::from_json
        ├── routine_comms.cpp              ← (pre-existing, unchanged)
        └── hub_override/
            └── hub.cpp                   ← uses resp.response instead of pull_measurement_data
```

---

## What Was Not Changed

| Component | Why untouched |
|-----------|---------------|
| `falcon-instrument-hub/runtime/internal/handlers/measure_command_handler.go` | The hub already sets `Response: respJSON` correctly. `Stream` and `Channel` remain empty — that is now the documented "inline" code path. |
| `falcon-comms/src/routine_comms.cpp` | `subscribe_measure_response` correctly receives the NATS message and calls `MeasureResponse::from_json`. No change needed — it now picks up `response` via the updated `from_json`. |
| `instrument-controller/tests/data-retrieval-1D/data-retrieval.cpp` | Test code itself is correct. The hang was in a library, not the test. |
| `instrument-script-server` (functional behaviour) | ISS logic was not changed. The `LOG_INFO` promotions and new checkpoints (items 7–10 above) are purely diagnostic and do not alter execution behaviour. |

---

## Future Work

| Item | Notes |
|------|-------|
| **JetStream path** | The `if (!resp.stream.empty())` branch in `hub.cpp` is dead code today. If large-payload streaming via JetStream is ever needed, the hub must publish to a configured stream and set `Stream`/`Channel` in `api.MeasureResponse`. |
| **Upstream PRs** | Changes to `MeasureResponse` struct and `from_json` should eventually be contributed back to `falcon-autotuning/falcon-comms` and `falcon-routine` to remove the need for the local overrides. |
| **Port payload refactor** | See `PORT_PAYLOAD_REFACTOR_NOTES.md` for the separate work needed before `request_port_payload()` can replace manual port construction in the test. |
