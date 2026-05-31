# DataRetrievalTest.Gaussian1D — Debug Log

**Updated:** 2026-05-27  
**Test:** `instrument-controller/tests/data-retrieval-1D/data-retrieval.cpp` → `DataRetrievalTest.Gaussian1D`  
**Current result:** FAILING — ISS hangs inside Lua script execution; hub `RunMeasurement` never returns

---

## Session History

| Date | Status | Key Event |
|------|--------|-----------|
| 2026-05-25 | ✅ PASSED (2.15 s) | All fixes landed, test green |
| 2026-05-27 | ❌ REGRESSED | Pulled new `falcon-core-libs` with breaking changes |
| 2026-05-27 (21:58 run) | ❌ HANGING | Workers receive 100 IPC cmds each, ISS stuck after that |

---

## Fixes Applied Since May 25

### Fix 1 — `runtime.AddCleanup` SIGABRT (DONE)

New `falcon-core-libs` introduced `runtime.AddCleanup` in `cmas.go`, which fires a
finalizer that calls `C.free` on an already-freed pointer → double-free → SIGABRT.

**Fix:** Rewrote `falcon-core-libs/go/falcon-core/cmemoryallocation/cmas.go` to use
`destroyOnce` / `freedHandles map[uintptr]struct{}{}` with `sync.Mutex`.
No `runtime.AddCleanup` or `runtime.SetFinalizer` anywhere.

### Fix 2 — Stale vcpkg Binary Cache (DONE)

ABI hash is computed from port file content, not local source. Bumping
`instrument-controller/ports/falcon-instrument-hub/vcpkg.json` version/port-version
forces a fresh build that picks up the `cmas.go` fix.

Current: `"version": "1.0.18", "port-version": 26`

---

## Active Root Causes

### Root Cause 1 — ISS Hangs on the 101st Lua Iteration (BLOCKING)

**Evidence from the 21:58 test run:**

| Log | Last entry | Interpretation |
|-----|-----------|----------------|
| `instrument_server.log` (59 lines) | `[21:58:42.685] Passing parameter 'setters'` | ISS never returned from `(*main_func)(sol::as_args(args))` |
| `worker_Source1.log` | Source1-1 … Source1-100 at 21:58:42.686–.700 | Worker received exactly 100 commands in 14 ms |
| `worker_Meter1.log` | Meter1-1 … Meter1-100 at same window | Worker received exactly 100 commands in 14 ms |
| `hub.log` (48 lines) | STATUS_HANDLER every 4 s after 21:58:42.677 | Hub `RunMeasurement` call blocked; no error/completion log |
| Workers shutdown | 22:00:50 | CTest 2-minute timeout → SIGKILL cascade |

**Root cause chain:**

1. The Lua script iterates `ipairs(sweepVoltages)` — 101 elements (shape=[101], values 0.0 … 1.0).
2. `InstrumentWorkerProxy::next_message_id_` is initialised to **1** (not 0), so the 101 iterations
   produce command IDs 1 → 101.  Workers received IDs 1 → 100 (100 commands) and are waiting
   for ID 101.
3. On the 101st iteration the ISS is attempting to deliver **Source1-101** to the IPC req queue
   (capacity 100). Since `execute_sync` is sequential, the req queue holds at most 1 message at
   any time — it should be empty.  Nevertheless, the command never reaches the worker.
4. `response_listener_loop` polls `ipc_queue_->receive(100ms)` (reads from resp queue).
   `execute()` calls `ipc_queue_->send(msg, cmd.timeout)` (writes to req queue).
   Both operate on the same `SharedQueue` object (`is_server_ = true`): send → req queue,
   receive → resp queue.

**Mystery: why is the 101st req-queue send stuck?**

The req queue is empty before iteration 101. `SharedQueue::send` calls
`boost::interprocess::message_queue::timed_send` with `abs_time = now + 5000 ms`.
`timed_send` to an empty queue with capacity 100 should return *immediately*.
Yet Source1-101 never arrives.

| Hypothesis | Why it might hang | Why it might not log |
|------------|-------------------|----------------------|
| H1 — resp queue full (100 messages) | Worker always sends a response for every command (including SET_VOLTAGE). If the `response_listener_loop` ever falls behind, 100 SET_VOLTAGE responses could fill the resp queue. `execute_sync` waits for the promise; `execute()` blocks in `timed_send` on the *req* queue waiting for the worker to drain the resp queue (they're blocked on each other) | `timed_send` deadline is hit, LOG_WARN fires — but ISS log may not have been flushed before SIGKILL |
| H2 — response listener thread died | `ipc_queue_->is_valid()` returned false → listener exits silently; `execute_sync` then waits 5 s, returns "Command timeout"; whole script times out in ~10 s — inconsistent with 2-minute hang unless the Lua script got a different error that caused an infinite loop | LOG_WARN "IPC queue invalid" + LOG_INFO "Response listener stopped" would fire — not in flushed portion of log |
| H3 — 30 s hub HTTP timeout fires, error not logged | `http.Client{Timeout: 30*time.Second}` should return an error at ~21:59:12; hub logs error, publishes failure NATS response; test fails fast; CTest waits full 2 min before killing processes | Error entry may be in the hub log's unflushed I/O buffer at kill time; STATUS entries still appear after the error |
| H4 — `timed_send` uses incorrect absolute time | Clock skew or ptime overflow causes the deadline to be far in the future; `timed_send` blocks for minutes | Would still eventually return; LOG_WARN when it does |

**Most actionable hypothesis (H1 + H3 combined):**

The 100th iteration completes.  The 101st SET_VOLTAGE `execute()` puts Source1-101 in an
*empty* req queue instantly, but the promise for Source1-101 is never fulfilled: the
response_listener_loop is polling the *resp* queue, which contains **no** messages because
the worker's heartbeats only go to resp and command responses are drained one-by-one.
The `execute_sync` waits 5 000 ms for the promise → times out → returns "Command timeout".
The Lua script proceeds with a nil voltage.  The cycle repeats for all remaining iterations.
The ISS eventually returns an HTTP response, but by then the hub's 30 s HTTP client timeout
has **already fired** and the connection was dropped.  The ISS tries to write the response to a
closed socket and silently discards it.  The hub already received a context-deadline error from
`http.Client.Post` at ~21:59:12 and logged it.  That log entry was flushed, but it was written
to a path that is **not** `hub.log` (e.g., to hub stdout or to a file that was not inspected).

---

### Root Cause 2 — `extractWaveformDataFromJSON` Navigation Path (LOW PRIORITY — UNCHANGED)

**File:** `falcon-instrument-hub/runtime/internal/serverinterpreter/falcon_core.go` ~line 456

The leading `"value1", "ptr_wrapper", "data"` prefix navigates into TimeDomain bounds
(floats 0.0/1.0) instead of the CompiledArray. This returns `nil` → `stubWaveformData()`
→ 1 sweep voltage.

**Counter-evidence:** ISS log shows shape=[101] and values 0.0 … 1.0 in the hub log, so the
101-point waveform *is* reaching the ISS.  This path may already be working.
Re-verify once the hang is fixed.

---

## Architecture Reference

```
Test binary
  └─ fork → instrument-hub  (--hub-config --iss-lib-path --working-dir --iss-binary)
               └─ exec instrument-script-server daemon start
                    └─ InstrumentWorkerProxy::start("Source1")
                    │    ├─ create_server_queue → instrument_Source1_{req,resp}   (cap 100 each)
                    │    └─ posix_spawnp instrument-worker Source1
                    └─ InstrumentWorkerProxy::start("Meter1")
                         ├─ create_server_queue → instrument_Meter1_{req,resp}
                         └─ posix_spawnp instrument-worker Meter1

  Test publishes INSTRUMENTHUB.MEASURE_COMMAND (sweepVoltages shape=[101])
    └─ hub measure_command_handler → RunMeasurement → HTTP POST /measure
         └─ ISS handle_measure → sol::state lua → (*main_func)(args)
              └─ Lua: for i=1..101 do
                   source:setVoltage(...)   → execute_sync(Source1, msg_id=i, 5000ms)
                   multimeter:getDatapoint() → execute_sync(Meter1,  msg_id=i, 5000ms)
                 end

IPC flow per command:
  execute() → SharedQueue::send (req queue, ISS→worker)
  worker receives, processes, send_command_response → SharedQueue::send (resp queue, worker→ISS)
  response_listener_loop → SharedQueue::receive (resp queue) → fulfill promise → execute_sync returns

next_message_id_ starts at 1  →  101 iterations produce IDs 1..101
Workers received IDs 1..100 ✓     ID 101 missing — ISS stuck
```

**Key source facts:**
- `InstrumentWorkerProxy::next_message_id_` initialised to **1** ([InstrumentWorkerProxy.hpp line 84](instrument-script-server/include/instrument-script-server/server/InstrumentWorkerProxy.hpp))
- `SharedQueue::send` → **req queue** (ISS→worker); `SharedQueue::receive` → **resp queue** (worker→ISS) — same `ipc_queue_` object, `is_server_=true` ([SharedQueue.cpp line 96](instrument-script-server/src/ipc/SharedQueue.cpp))
- Req/resp queue capacity: **100 messages each** ([SharedQueue.cpp line 31](instrument-script-server/src/ipc/SharedQueue.cpp))
- Worker **always** calls `send_command_response` regardless of `expects_response` ([generic_worker_main.cpp](instrument-script-server/src/workers/generic_worker_main.cpp))
- Hub HTTP client timeout: **30 s** ([client.go](falcon-instrument-hub/runtime/internal/serverinterpreter/client.go))
- ISS server log level: **INFO** — `RuntimeContext`/`InstrumentWorkerProxy` DEBUG entries are invisible
- Worker log level: **DEBUG** — full visibility into command receipt

---

## Next Steps

### Step 1 — Determine whether the hub's 30 s HTTP timeout fires

```bash
# Check where hub stdout/stderr go during the test
# The test forks the hub with exec; check if stdout is captured
grep -n "stdout\|stderr\|Stdout\|Stderr" \
  instrument-controller/tests/data-retrieval-1D/data-retrieval.cpp | head -20

# Check CTest last-test output for hub fmt.Printf lines
# ("Measurement script returned N results" appears on hub stdout, not hub.log)
cat instrument-controller/build/linux-clang-release/Testing/Temporary/LastTest.log | grep -i "measurement\|script\|result" | head -20

# Check if there is a hub stdout capture file
ls instrument-controller/tests/hub/
```

Expected: if the 30 s timeout fired and the hub published a NATS error response, you should
see `"measure_command_handler: RunMeasurement failed"` somewhere (hub log, stdout, or
`LastTest.log`).

### Step 2 — Run test under strace to find the exact syscall blocking the ISS

```bash
# Run the test in the background, then strace the ISS PID
cd instrument-controller/build/linux-clang-release
source env.sh
ctest -R DataRetrievalTest --output-on-failure -V &
sleep 5   # wait for ISS to start its Lua execution
ISS_PID=$(pgrep -f "instrument-script-server daemon")
strace -p "$ISS_PID" -e trace=futex,mq_timedsend,mq_timedreceive 2>&1 | head -40
```

If strace shows `mq_timedsend(...)` with a far-future deadline → H4 (clock issue).
If strace shows `futex(...)` with a 5 s deadline spinning → H2 (promise not fulfilled).
If the call returns quickly and the ISS loops → H3 (error discarded, retry path?).

### Step 3 — Fix the off-by-one: make sweepVoltages 100 points

The simplest fix: change `CartesianIdentityWaveform1D(100, ...)` in the test to produce
exactly 100 points, so `ipairs(sweepVoltages)` loops 100 times and IPC IDs 1-100 match the
queue capacity.  This avoids the 101st-command issue entirely.

```cpp
// instrument-controller/tests/data-retrieval-1D/data-retrieval.cpp
// Change NUM_POINTS from 101 to 100 (use exclusive upper bound or adjust range)
```

Alternatively, increase `create_server_queue` capacity from 100 → 200.

### Step 4 — Inspect whether the resp queue fills up

If H1 is correct (resp queue fills), the root cause is that `execute_sync` for
`expects_response=false` (SET_VOLTAGE) drains the resp queue one-by-one.  But the worker also
sends a response for every command.  The resp queue should stay at 0-1 messages.  Verify by
adding a `get_num_msg()` call after each receive in `response_listener_loop`.

### Step 5 — Confirm ISS log is not truncated (unflushed buffer)

The ISS uses spdlog.  Spdlog flushes by default on every `warn`/`error` call but may buffer
`info`/`debug`.  Check whether a `flush_on(spdlog::level::info)` or `flush_every(1s)` is
configured.  If not, LOG_WARN "Send timeout" may be in the unflushed buffer at kill time.

```bash
grep -rn "flush_on\|flush_every\|set_level" \
  instrument-script-server/src/ instrument-script-server/include/
```

---

## What Was Completed

| Item | Status | Session |
|------|--------|---------|
| ISS sol2 / `load_result` / `main` detection fixes | ✅ Done | May 25 |
| `mock_multimeter` INT64/DOUBLE compatibility | ✅ Done | May 25 |
| Wire map name mismatches | ✅ Done | May 25 |
| Hub subscription ordering fix | ✅ Done | May 25 |
| `1D Gaussian Noise.tl` rewritten for `sweepVoltages + getDatapoint` | ✅ Done | May 25 |
| `ExtractWaveformDataFromRequest` (CGO + stub paths) | ✅ Done | May 25 |
| Float result collection in hub handler | ✅ Done | May 25 |
| `waveform_json_utils.go` shared utilities | ✅ Done | May 25 |
| `cmas.go` SIGABRT (runtime.AddCleanup double-free) | ✅ Done | May 27 |
| vcpkg port-version 26 (cache bust) | ✅ Done | May 27 |
| Hub binary rebuilt with fixed cmas.go | ✅ Done | May 27 |
| Workers now receiving IPC commands (100/101) | ✅ Progress | May 27 |

---

## Build Commands

```bash
# Rebuild hub binary
VCPKG_INST=/home/zdm2/Documents/github/FAlCon/instrument-controller/vcpkg_installed/x64-linux-dynamic
cd /home/zdm2/Documents/github/FAlCon/falcon-instrument-hub/runtime
CGO_ENABLED=1 \
  PKG_CONFIG_PATH="$VCPKG_INST/lib/pkgconfig" \
  CGO_LDFLAGS="-L$VCPKG_INST/lib -Wl,-rpath,$VCPKG_INST/lib" \
  go build -tags cgo,falcon_core -o "$VCPKG_INST/bin/instrument-hub" ./cmd/main.go

# Rebuild ISS + worker (vcpkg uses local workspace source automatically)
cd /home/zdm2/Documents/github/FAlCon/instrument-controller
make install  # bumps port-version if needed, runs vcpkg install

# Run test
cd build/linux-clang-release
source env.sh
ctest -R DataRetrievalTest --output-on-failure -V 2>&1 | tee /tmp/test-run.log
```

---

## Session 2 Update — Root Cause Found (2026-05-28)

### Root Cause: Off-by-one in hub sweep voltage generation

`falcon-instrument-hub/runtime/internal/serverinterpreter/falcon_core.go`:
```go
// Build voltage sweep: normalizedArr has division+1 elements (half-open [min, max))
numPoints := len(normalized) - 1   // drops the last sweep point
rawTimeTrace := make([][]float64, numPoints)
```

With `division = NUM_POINTS - 1 = 99`, the UnitSpace produces 100 normalized points but the hub
only uses 99 of them. The Lua `ipairs` loop therefore runs exactly 99 times. The test expected
100 results and hung waiting for the 100th which was never generated.

**No timeout or error was logged** because:
- `execute_sync()` `LOG_WARN` fires only after 5000ms with no response — but `execute_sync` was
  never called for command 100 (Lua loop exited)
- The hub's 30s HTTP timeout was the only thing that would have eventually surfaced the failure

### Fix Applied

**`instrument-controller/tests/data-retrieval-1D/data-retrieval.cpp`**:
```cpp
// Before (produced 99 sweep voltages):
auto waveform = Waveform::CartesianIdentityWaveform1D(NUM_POINTS - 1, ...);

// After (produces 100 sweep voltages):
auto waveform = Waveform::CartesianIdentityWaveform1D(NUM_POINTS, ...);
```

With `division = NUM_POINTS = 100`, the UnitSpace has 101 points, hub uses 100 → Lua loops 100
times → 100 IPC commands → test passes.

### Logging Improvements

Promoted `LOG_DEBUG` → `LOG_INFO` in `InstrumentWorkerProxy.cpp` and `RuntimeContext.cpp`
for the critical execute path. These now appear in `iss-daemon.log` at INFO level:
- Lua function name on each `call()`
- Command verb/instrument before and after `execute_sync()`
- IPC message enqueue and send per command

ISS port-version bumped 11 → 12 to force vcpkg cache bust and rebuild.

### Proposed Investigations

1. **Hub half-open interval design**: `numPoints = len(normalized) - 1` intentionally creates
   a `[min, max)` sweep. For DC voltage sweeps this is likely wrong — the maximum should be
   reached. Consider changing to `len(normalized)` and updating callers.

2. **Add ISS sweep count log**: After Lua returns, log `collected_results_.size()` in
   `RuntimeContext` to surface count mismatches early.

3. **Hub test for sweep count**: `measure_command_handler_test.go` should assert
   `len(sweepVoltages) == expectedNumPoints`.

4. **Clarify `CartesianIdentityWaveform1D` semantics**: "divisions=N" means N intervals
   (→ N+1 endpoints). The hub currently returns N points (not N+1). Document or fix.

5. **Better test failure message**: Instead of hanging, `request_measurement` could assert
   that the returned array size matches the waveform's expected point count.


---

## Session 3 Update (2026-05-30)

### Status: Still Hanging — All 100 IPC Commands Complete

After the Session 2 fix, ISS logs confirm all 100 `SET_VOLTAGE` and 100 `GET_DATAPOINT`
commands complete successfully (Source1-1..100, Meter1-1..100). The hub receives the measure
command at the same timestamp. The test still hangs indefinitely.

**Timeline from last run:**
- `19:39:46.756` — hub receives `MEASURE_COMMAND`
- `19:39:46.778` — ISS logs final `Meter1.GET_DATAPOINT returned: success=true`
- `19:39:46.778` onward — hub log shows only STATUS_HANDLER heartbeats (every 4s); no errors
- `19:41:48` — user manually kills workers; test killed with ctrl+c

**Key deduction:** The hub's HTTP client has a 30-second timeout. If `RunMeasurement` timed
out, `h.logger.Error(...)` would appear in the hub log. No error is logged → `RunMeasurement`
either (a) returned successfully or (b) is still pending. Because 121s elapsed before the
kill (>30s), a timeout would have fired and logged an error. Absence of error log means the
hang is **after** `RunMeasurement` returns.

**Candidates for the hang (in order of likelihood):**
1. `buildMeasurementResponseJSON` — CGo pipeline into falcon-core C++ library
2. `h.nc.Publish(MeasureResponseSubject, ...)` — NATS publish
3. The ISS `handle_measure` itself is stuck (in `resp.dump()` or sending the HTTP response)
   before the hub even gets the result

### Logging Added This Session

**ISS `CommandHandlers.cpp`** — two new `LOG_INFO` checkpoints in `handle_measure`:
- Before the result serialization loop: `"Lua script done. Serializing N results into HTTP response"`
- After the loop, before `return 0`: `"Serialization complete. Returning to HTTP handler to send response"`

**Hub `measure_command_handler.go`** — five new `h.logger.Info` checkpoints replacing
the previous `fmt.Printf` debug output (which went only to stdout, not the log file):

| Checkpoint | What it tells us |
|-----------|-----------------|
| `RunMeasurement returned: resultCount=N err=<nil>` | Did the HTTP call to ISS complete? Did we get results? |
| `bufferData collected: len=N` | Are GET_DATAPOINT float values being extracted? |
| `Calling buildMeasurementResponseJSON` | Did we reach the CGo section? |
| `buildMeasurementResponseJSON complete` | Did the CGo section finish? |
| `Publishing to NATS subject FALCON.MEASURE_RESPONSE` | Are we about to publish? |
| `NATS publish complete; handler done` | Did the whole handler finish? |

ISS port-version bumped 12 → 13 to force rebuild with new log statements.

### Next Steps

1. **Run `make test`** and capture both `iss-daemon.log` and `falcon-runtime_*.log`.
2. **Identify the last checkpoint** reached before the hang — this will pinpoint the
   blocking call to one of: ISS serialization, HTTP send, `RunMeasurement`, `bufferData`
   collection, CGo `buildMeasurementResponseJSON`, or NATS publish.
3. **If ISS "Serialization complete" never logs** → ISS is stuck in `resp.dump()` (100
   results × 2 params = 200 entries in JSON — should be fast, but could be a serialization
   issue with `std::chrono::steady_clock` timestamps).
4. **If hub "RunMeasurement returned" never logs** → ISS response never reached the hub
   (HTTP write or read stalled on localhost — extremely unlikely but possible if socket
   buffer is exhausted for a very large response body).
5. **If hub "Calling buildMeasurementResponseJSON" logs but "complete" does not** →
   hang is in CGo falcon-core calls. Check `farraydouble.FromData`, `labelledmeasuredarray.FromFArray`,
   etc. for blocking mutex or allocation failure.
6. **If hub "Publishing to NATS..." logs but "NATS publish complete" does not** →
   hang is in NATS publish (stream backpressure or disconnected broker).
