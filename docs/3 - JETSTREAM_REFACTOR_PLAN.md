# JetStream Refactor Plan — `DataRetrievalTest.Gaussian1D`

**Date:** 2026-05-30  
**Goal:** Remove the inline `response` field workaround. Make the hub publish `respJSON` to a
JetStream subject and set `Stream`/`Channel` in `api.MeasureResponse` so the original
`falcon-comms` and `falcon-routine` code paths work as designed, without any local C++ overrides.

---

## Premise

The original `falcon-comms` and `falcon-routine` are **correct as written**:

- `pull_measurement_data(stream, channel, batch_size)` → `js_PullSubscribe(js, stream, channel, ...)` is the right mechanism.
- `request_measurement` calling `pull_measurement_data(resp.stream, resp.channel, 1)` is the right call sequence.
- The C++ `MeasureResponse` struct having `stream` and `channel` (but no `response`) is the right struct.

The hub must align to these. The only code that needs to change is in Go.

---

## Root Cause (Recap)

`falcon-instrument-hub/runtime/internal/handlers/measure_command_handler.go` assembled:

```go
measureResp := api.MeasureResponse{
    Response:  respJSON,
    Timestamp: time.Now().UnixMicro(),
    Hash:      cmd.Hash,
    // Stream and Channel were never set → both ""
}
```

The hub never published `respJSON` to JetStream, leaving `Stream` and `Channel` empty.
The C++ client then called `js_PullSubscribe(js, "", "", ...)` which blocks indefinitely in cnats.

The `Response` field workaround (Phase 4b) patched around this on the C++ side. This plan
removes that workaround and fixes the root cause on the hub side.

---

## Data Flow After Refactor

```
hub computes respJSON
  │
  ├─ js.Publish("FALCON.MEASURE_DATA.<hash>", respJSON)   ← durable in JetStream stream FALCON_MEASURE
  │
  └─ nc.Publish("FALCON.MEASURE_RESPONSE", {Stream: "FALCON.MEASURE_DATA.<hash>", Channel: "", ...})
                                                           ← standard NATS (triggers C++ client)
C++ receives FALCON.MEASURE_RESPONSE
  └─ pull_measurement_data("FALCON.MEASURE_DATA.<hash>", "", 1)
       └─ js_PullSubscribe(js, "FALCON.MEASURE_DATA.<hash>", "", NULL, NULL, NULL)
            └─ cnats looks up stream covering subject "FALCON.MEASURE_DATA.*" → finds FALCON_MEASURE
            └─ creates ephemeral pull consumer
            └─ pulls 1 message → respJSON
            └─ MeasurementResponse::from_json_string → test assertions pass
```

**Key ordering constraint:** The JetStream publish must happen *before* the NATS
`FALCON.MEASURE_RESPONSE` publish. This ensures the message is durably stored before the C++
subscriber arrives to pull it.

---

## Files Changed

### Phase 1 — Revert local C++ overrides

---

#### 1. Delete `falcon/comms/include/falcon-comms/commands_definitions.hpp`

This file adds a `response` field to the C++ `MeasureResponse` struct as a workaround.
Deleting it restores the upstream struct (which has `stream` and `channel` only).

The portfile references this file under an `if(EXISTS ...)` guard, so it will silently skip
the copy once the file is gone.

---

#### 2. Delete `falcon/comms/src/commands.cpp`

This file serialises/deserialises the `response` field in `MeasureResponse::to_json` and
`from_json`. Deleting it restores the upstream implementation.

---

#### 3. Delete `falcon/comms/src/hub_override/hub.cpp`

This file overrides `request_measurement` in `falcon-routine` with a branch that uses
`resp.response` directly when `stream` is empty. Once the hub sets `stream` correctly, this
branch is never needed.

The `falcon-routine` portfile already guards the injection with `if(EXISTS ...)`. Once the
file is gone, the upstream `hub.cpp` is used — which calls `pull_measurement_data`
unconditionally, as intended.

---

#### 4. Edit `instrument-controller/ports/falcon-comms/portfile.cmake`

Remove the Phase 4b `file(COPY ...)` blocks added for `commands_definitions.hpp` and
`commands.cpp`. The `routine_comms` overrides are unrelated and must stay.

```cmake
# REMOVE these two blocks:
set(FALCON_COMMS_CMD_HDR_OVERRIDE "${WORKSPACE_ROOT}/falcon/comms/include/falcon-comms/commands_definitions.hpp")
if(EXISTS "${FALCON_COMMS_CMD_HDR_OVERRIDE}")
    file(COPY "${FALCON_COMMS_CMD_HDR_OVERRIDE}" DESTINATION "${SOURCE_PATH}/include/falcon-comms")
endif()

set(FALCON_COMMS_CMD_SRC_OVERRIDE "${WORKSPACE_ROOT}/falcon/comms/src/commands.cpp")
if(EXISTS "${FALCON_COMMS_CMD_SRC_OVERRIDE}")
    file(COPY "${FALCON_COMMS_CMD_SRC_OVERRIDE}" DESTINATION "${SOURCE_PATH}/src")
endif()
```

---

#### 5. `instrument-controller/ports/falcon-comms/vcpkg.json`

Bump `port-version` to force vcpkg to rebuild `falcon-comms` against the unmodified upstream
struct (without `response` field):

```json
// Before
"port-version": 3

// After
"port-version": 4
```

---

#### 6. `instrument-controller/ports/falcon-routine/vcpkg.json`

Bump `port-version` to force vcpkg to rebuild `falcon-routine` using the upstream `hub.cpp`
(the override file will be absent):

```json
// Before
"port-version": 2

// After
"port-version": 3
```

---

### Phase 2 — Hub JetStream implementation

**Single file:** `falcon-instrument-hub/runtime/internal/handlers/measure_command_handler.go`

---

#### 7. Add `js nats.JetStreamContext` to `MeasureCommandHandler`

```go
// Before
type MeasureCommandHandler struct {
    logger             *logging.Logger
    nc                 *nats.Conn
    subscription       *nats.Subscription
    measurementManager *measurements.Manager
    instrumentHandler  *instrument.Handler
    busyManager        BusyManager
    dispatcher         MeasurementDispatcher
    wireMap            *config.WireMap
}

// After
type MeasureCommandHandler struct {
    logger             *logging.Logger
    nc                 *nats.Conn
    js                 nats.JetStreamContext   // ← add
    subscription       *nats.Subscription
    measurementManager *measurements.Manager
    instrumentHandler  *instrument.Handler
    busyManager        BusyManager
    dispatcher         MeasurementDispatcher
    wireMap            *config.WireMap
}
```

---

#### 8. Initialise JetStream in `Subscribe()` and ensure stream exists

After `h.nc = nc`, add:

```go
h.js, err = nc.JetStream()
if err != nil {
    return fmt.Errorf("failed to create JetStream context: %w", err)
}

_, addErr := h.js.AddStream(&nats.StreamConfig{
    Name:     "FALCON_MEASURE",
    Subjects: []string{"FALCON.MEASURE_DATA.*"},
    MaxAge:   60 * time.Second,
})
if addErr != nil && addErr != nats.ErrStreamNameAlreadyInUse {
    return fmt.Errorf("failed to ensure FALCON_MEASURE stream: %w", addErr)
}
```

Notes:
- `MaxAge: 60 * time.Second` prevents stale measurement data accumulating across test runs.
- `nats.ErrStreamNameAlreadyInUse` is tolerated so repeated test runs don't fail on stream
  re-creation.
- The NATS server already has `JetStream: true` (set in `networking/nats.go`). No server-side
  changes needed.

---

#### 9. Publish `respJSON` to JetStream before publishing `FALCON.MEASURE_RESPONSE`

After `respJSON` is built and before `measureResp := api.MeasureResponse{...}`:

```go
measureSubject := "FALCON.MEASURE_DATA." + strconv.FormatInt(cmd.Hash, 10)
if _, err := h.js.Publish(measureSubject, []byte(respJSON)); err != nil {
    h.logger.Error(MeasureCommandHandlerName,
        fmt.Sprintf("failed to publish measurement to JetStream: %v", err))
    return
}
```

---

#### 10. Set `Stream` in the `api.MeasureResponse` literal

```go
// Before
measureResp := api.MeasureResponse{
    Response:  respJSON,
    Timestamp: time.Now().UnixMicro(),
    Hash:      cmd.Hash,
}

// After
measureResp := api.MeasureResponse{
    Stream:    measureSubject,   // ← non-empty; C++ will pull from here
    Response:  respJSON,         // kept for backward compatibility
    Timestamp: time.Now().UnixMicro(),
    Hash:      cmd.Hash,
    // Channel stays "" → ephemeral pull consumer
}
```

---

## Design Decisions

| Decision | Rationale |
|----------|-----------|
| **Subject: `FALCON.MEASURE_DATA.<hash>`** | `cmd.Hash` is already unique per request. No UUID needed. |
| **Stream name: `FALCON_MEASURE`** | Covers `FALCON.MEASURE_DATA.*` with a single persistent stream. |
| **`Channel = ""`** | Empty durable name → `js_PullSubscribe` creates an ephemeral pull consumer automatically. No explicit consumer management needed. |
| **`MaxAge: 60s`** | Long enough for any test run; prevents data accumulation across runs. Acknowledged messages (the C++ side already calls `natsMsg_Ack`) are not deleted immediately but will expire. |
| **Keep `Response` field in Go struct** | `api.MeasureResponse.Response` is still set. This costs nothing and avoids breaking any tooling or future use that reads it. The C++ side simply doesn't use it once `stream` is non-empty. |
| **JetStream publish BEFORE NATS response** | The JetStream message must be durably stored before the C++ subscriber arrives. `js.Publish` returns a `PubAck` confirming durability. |
| **Tolerate `ErrStreamNameAlreadyInUse`** | Idiomatic for services that restart without cleaning up their streams. |

---

## What Is Not Changed

| Component | Why |
|-----------|-----|
| `falcon-comms` upstream source | Correct as-is. `pull_measurement_data` → `js_PullSubscribe` is the right design. |
| `falcon-routine` upstream `hub.cpp` | Correct as-is. `pull_measurement_data(resp.stream, resp.channel, 1)` is the right call. |
| `falcon/comms/src/routine_comms.cpp` | Pre-existing override; unrelated to this refactor. |
| `falcon/comms/include/falcon-comms/routine_comms.hpp` | Pre-existing override; unrelated to this refactor. |
| `instrument-script-server` | ISS is correct. The diagnostic `LOG_INFO` changes from session 3 remain. |
| `falcon-instrument-hub/runtime/internal/networking/nats.go` | JetStream is already enabled on the embedded server (`JetStream: true`). No changes needed. |
| `instrument-controller/tests/data-retrieval-1D/data-retrieval.cpp` | Test code is correct. |

---

## Verification

```bash
cd /home/zdm2/Documents/github/FAlCon/instrument-controller
make test
```

Expected:
- `DataRetrievalTest.Gaussian1D` → **PASSED** (no hang)
- Hub log (`hub/log/falcon-runtime_*.log`) shows:
  1. `"Publishing measurement to JetStream subject FALCON.MEASURE_DATA.<hash>"` ← appears first
  2. `"Publishing to NATS subject FALCON.MEASURE_RESPONSE"` ← appears second
- No local C++ override files remain under `falcon/comms/`
