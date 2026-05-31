# JetStream Refactor Changes

## Overview

This document records the code changes made to implement proper NATS JetStream
delivery of measurement data from `falcon-instrument-hub` to
`instrument-controller`, and to fix the test hang that existed both before and
after that refactor.

---

## Background

The `instrument-controller` test `DataRetrievalTest.Gaussian1D` exercises the
full measurement pipeline:

1. Controller calls `request_measurement` (C++ / `falcon-routine`).
2. `falcon-comms` `RoutineComms` publishes `INSTRUMENTHUB.MEASURE_COMMAND`.
3. Hub (`falcon-instrument-hub`) receives the command, runs the measurement
   script, and publishes a `FALCON.MEASURE_RESPONSE`.
4. `RoutineComms::subscribe_measure_response` unblocks with the response struct.
5. The response contains a `stream` field — a JetStream subject — and
   `RoutineComms::pull_measurement_data` pulls the measurement JSON from that
   subject using cnats JetStream.
6. The measurement JSON is deserialised into a `MeasurementResponse`.

Prior to these changes two workarounds existed that short-circuited steps 4–6:

- A **Phase 4b override** embedded the full `MeasurementResponse` JSON directly
  in the `response` field of `FALCON.MEASURE_RESPONSE`, letting the controller
  skip the JetStream pull entirely.
- A **`hub.cpp` override** checked `resp.stream.empty()` and used
  `resp.response` (the inline JSON) when the stream was empty, also bypassing
  the pull.

These workarounds kept the test green but diverged from the upstream
`falcon-comms` / `falcon-routine` design, which expects the response JSON to
arrive exclusively via JetStream pull.

---

## Changes

### 1. `falcon-instrument-hub/runtime/internal/handlers/measure_command_handler.go`

**What changed**

- Added a `js nats.JetStreamContext` field to `MeasureCommandHandler`.
- In `Subscribe()`: after establishing the NATS connection, create a JetStream
  context and ensure the `FALCON_MEASURE` stream exists
  (`subjects: ["FALCON.MEASURE_DATA.*"]`, `MaxAge: 60s`).
- In `handleMessage()`: after building `respJSON`, publish it to
  `FALCON.MEASURE_DATA.<hash>` via JetStream *before* publishing to
  `FALCON.MEASURE_RESPONSE`. Set `Stream` on the `MeasureResponse` struct to
  that JetStream subject so the controller knows where to pull.

**Why**

The upstream `falcon-routine` `hub.cpp` calls
`pull_measurement_data(resp.stream, resp.channel, 1)` unconditionally. For that
call to succeed, the hub must have published the measurement data to a JetStream
subject *and* set `Stream` to a non-empty value before publishing the NATS
response. Previously the hub never wrote to JetStream, so `resp.stream` was
always empty and the pull was skipped by the local override.

---

### 2. Deleted: `falcon/comms/include/falcon-comms/commands_definitions.hpp`

**What changed**

Removed the local override that added a `response` field to the
`MeasureResponse` struct.

**Why**

The upstream `MeasureResponse` struct already has `stream` and `channel` fields.
Adding `response` was the Phase 4b workaround to carry inline JSON. Now that the
hub publishes to JetStream and sets `stream`, the `response` field is no longer
needed and re-adding it would break the upstream deserialisation logic.

---

### 3. Deleted: `falcon/comms/src/commands.cpp`

**What changed**

Removed the local override that serialised / deserialised the `response` field.

**Why**

Same reason as above — the upstream `commands.cpp` correctly handles `stream`
via `j.contains("stream")`. The override existed only to support the now-removed
`response` field.

---

### 4. Deleted: `falcon/comms/src/hub_override/hub.cpp`

**What changed**

Removed the local `hub.cpp` override that used `resp.response` (inline JSON)
when `resp.stream` was empty.

**Why**

With the hub now setting `resp.stream` to a valid JetStream subject, the
upstream `hub.cpp` path (`pull_measurement_data(resp.stream, resp.channel, 1)`)
is always taken. The local override that guarded the inline path is no longer
needed and would shadow the upstream file.

---

### 5. Created: `falcon/comms/src/natsManager.cpp`

**What changed**

Added a local override of `NatsManager::jetstream_pull` that replaces the
upstream body of that function. All other methods are copied verbatim from the
upstream file.

The single change is in `jetstream_pull`:

```cpp
// Upstream (broken in cnats v3):
for (int i = 0; i < batch_size; ++i) {
    natsMsg *msg = nullptr;
    s = natsSubscription_NextMsg(&msg, sub, 1000); // returns NATS_INVALID_SUBSCRIPTION immediately
    ...
}

// Override (correct for cnats v3):
natsMsgList list;
memset(&list, 0, sizeof(list));
s = natsSubscription_Fetch(&list, sub, batch_size, 5000, nullptr);
if (s == NATS_OK) {
    for (int i = 0; i < list.Count; i++) {
        if (list.Msgs[i] != nullptr) {
            messages.emplace_back(natsMsg_GetData(list.Msgs[i]),
                                  natsMsg_GetDataLength(list.Msgs[i]));
            natsMsg_Ack(list.Msgs[i], nullptr);
        }
    }
    natsMsgList_Destroy(&list);
} else if (s != NATS_TIMEOUT) {
    natsMsgList_Destroy(&list);
    spdlog::error("JetStream fetch failed: {}", natsStatus_GetText(s));
}
```

**Why**

In cnats v3.12.0, `natsSubscription_NextMsg` explicitly rejects pull
subscriptions: inside `natsSub_nextMsg` the flag check

```c
else if (!pullSubInternal && sub->jsi->pull)
    return nats_setError(NATS_INVALID_SUBSCRIPTION, "%s",
                         jsErrNotApplicableToPullSub);
```

causes it to return `NATS_INVALID_SUBSCRIPTION` immediately, without fetching
any messages. The for-loop would break on the first iteration and return an
empty vector. The correct cnats v3 API for pull consumers is
`natsSubscription_Fetch`, which sends the actual server-side fetch request
through the subscription's `nxtMsgSubj` inbox and waits up to the given timeout
for messages to arrive.

---

### 6. `instrument-controller/ports/falcon-comms/portfile.cmake`

**What changed**

- Removed the `file(COPY ...)` blocks for `commands_definitions.hpp` and
  `commands.cpp` (Phase 4b overrides, now deleted).
- Added a `file(COPY ...)` block for the new `natsManager.cpp` override.

**Why**

The portfile is the injection point that copies local override files into the
upstream vcpkg source tree before the CMake build. Each override file needs a
corresponding entry here; deleted overrides need their entries removed to prevent
stale copies.

---

### 7. `instrument-controller/ports/falcon-comms/vcpkg.json` — `port-version: 6`

**What changed**

Bumped `port-version` from 3 → 4 → 5 → 6 across the session.

**Why**

vcpkg caches installed ports by version. Bumping `port-version` forces a clean
reinstall of `falcon-comms` so that the updated portfile and override files are
used. Each bump corresponded to a distinct iteration of the override:
- 4: Phase 4b blocks removed, no `natsManager.cpp` yet.
- 5: `natsManager.cpp` added (initial version with `jsErrCode jerr = 0;`, which
  failed to compile because `jsErrCode` is a `uint32_t` typedef that cannot be
  direct-initialised from `int` in C++).
- 6: `natsManager.cpp` fixed — `nullptr` passed instead of `&jerr`.

---

### 8. `instrument-controller/ports/falcon-routine/vcpkg.json` — `port-version: 3`

**What changed**

Bumped `port-version` from 2 → 3.

**Why**

The `falcon-routine` port includes the now-deleted `hub.cpp` override path in
its portfile. Bumping the version forces a reinstall using the upstream
`hub.cpp`, which calls `pull_measurement_data` unconditionally with `resp.stream`
and `resp.channel`.

---

## Net Result

| Component | Before | After |
|-----------|--------|-------|
| Hub publishes measurement JSON | NATS only (inline in `response` field) | JetStream (`FALCON.MEASURE_DATA.<hash>`) **and** NATS response with `stream` set |
| `MeasureResponse.stream` on controller side | Always empty (`""`) | `"FALCON.MEASURE_DATA.<hash>"` |
| C++ pull path | Skipped by `hub.cpp` override | Taken unconditionally via upstream `hub.cpp` |
| `NatsManager::jetstream_pull` | Used `natsSubscription_NextMsg` (invalid for pull consumers in cnats v3) | Uses `natsSubscription_Fetch` (correct cnats v3 pull API) |
| Test result | Passing via workaround | Passing via correct JetStream path |
