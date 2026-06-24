# SetVoltage / Gaussian1DMeasureGetSet Cross-Test Response Investigation

## Summary

The new failure does not look like a logic bug inside `Gaussian1DMeasureGetSet` itself.

The stronger signal is that the second test is consuming a stale measurement response that matches the earlier `SetVoltage` test:

- `instrument_type()` is `dc_voltage_source`
- array size is `1`

That is the exact response shape we expect from `SetVoltage`, not from `Gaussian1DMeasureGetSet`, which expects:

- `InstrumentTypes::VOLTMETER`
- `NUM_POINTS == 100`

## Evidence From The Two Logs

### Old log: `SetVoltage` passes when later tests are skipped

From [`make_text_out_old.txt`](/home/zdm2/Documents/github/FAlCon/instrument-controller/tests/hub/log/make_text_out_old.txt):

- `DataRetrievalTest.SetVoltage` passes
- `DataRetrievalTest.Gaussian1DMeasureGetSet` is skipped
- the run ends cleanly

This means the `SetVoltage` path can succeed by itself.

### New log: `Gaussian1DMeasureGetSet` gets a `SetVoltage`-shaped result

From [`make_text_out.txt`](/home/zdm2/Documents/github/FAlCon/instrument-controller/tests/hub/log/make_text_out.txt):

- `DataRetrievalTest.SetVoltage` passes first
- `DataRetrievalTest.Gaussian1DMeasureGetSet` then fails with:
  - `labelledArray->instrument_type() == "dc_voltage_source"`
  - `labelledArray->size() == 1`

Those values line up with the `SetVoltage` response, not with the Gaussian measurement test.

### The second test appears to complete its response path too quickly

In the same new log:

- the test subscribes to `FALCON.MEASURE_RESPONSE`
- it unsubscribes almost immediately
- `JetStream initialized` appears
- the assertion failure follows right away

At the same time, the runtime log for that test process only contains startup entries:

- [`falcon-runtime_2026-06-20_18-21-18.log`](/home/zdm2/Documents/github/FAlCon/instrument-controller/tests/hub/log/falcon-runtime_2026-06-20_18-21-18.log)

It does **not** show a fresh `MEASURE_COMMAND_HANDLER` request being processed for `measure_get_set`.

That strongly suggests the controller side accepted an already-available response instead of waiting for a newly produced Gaussian measurement.

## Why This Can Happen

## 1. `RoutineComms` accepts the first `FALCON.MEASURE_RESPONSE` message without correlation

In [`routine_comms.cpp`](/home/zdm2/Documents/github/FAlCon/falcon-comms/src/routine_comms.cpp#L19):

- `subscribe_measure_response(...)` subscribes to `FALCON.MEASURE_RESPONSE`
- the callback accepts the **first** message it receives
- it does **not** check:
  - request timestamp
  - request hash
  - stream subject
  - channel / consumer id

Relevant lines:

- [`routine_comms.cpp:19`](/home/zdm2/Documents/github/FAlCon/falcon-comms/src/routine_comms.cpp#L19)
- [`routine_comms.cpp:28`](/home/zdm2/Documents/github/FAlCon/falcon-comms/src/routine_comms.cpp#L28)
- [`routine_comms.cpp:44`](/home/zdm2/Documents/github/FAlCon/falcon-comms/src/routine_comms.cpp#L44)

This means any delayed or stale `FALCON.MEASURE_RESPONSE` can satisfy the waiting future.

## 2. The hub publishes a hash, but the controller does not validate it

The hub includes `Hash` in `api.MeasureResponse`:

- [`api.go:186`](/home/zdm2/Documents/github/FAlCon/falcon-instrument-hub/runtime/internal/api/api.go#L186)

And `measure_command_handler.go` publishes that hash into the response:

- [`measure_command_handler.go:273`](/home/zdm2/Documents/github/FAlCon/falcon-instrument-hub/runtime/internal/handlers/measure_command_handler.go#L273)
- [`measure_command_handler.go:575`](/home/zdm2/Documents/github/FAlCon/falcon-instrument-hub/runtime/internal/handlers/measure_command_handler.go#L575)

But the C++ `RoutineComms` side ignores it completely.

So the protocol already has correlation data, but the consumer is not using it.

## 3. The hub does not populate `Channel`, so JetStream pulls are under-correlated

`api.MeasureResponse` includes a `Channel` field:

- [`api.go:188`](/home/zdm2/Documents/github/FAlCon/falcon-instrument-hub/runtime/internal/api/api.go#L188)

But the hub response construction shown in both paths sets:

- `Stream`
- `Response`
- `Timestamp`
- `Hash`

and does **not** set `Channel`.

Relevant lines:

- [`measure_command_handler.go:273`](/home/zdm2/Documents/github/FAlCon/falcon-instrument-hub/runtime/internal/handlers/measure_command_handler.go#L273)
- [`measure_command_handler.go:575`](/home/zdm2/Documents/github/FAlCon/falcon-instrument-hub/runtime/internal/handlers/measure_command_handler.go#L575)

Then the controller calls:

- [`hub.cpp:55`](/home/zdm2/Documents/github/FAlCon/falcon-routine/src/hub.cpp#L55)
- [`routine_comms.cpp:66`](/home/zdm2/Documents/github/FAlCon/falcon-comms/src/routine_comms.cpp#L66)

which becomes:

- `pull_measurement_data(resp.stream, resp.channel, 1)`

If `resp.channel` is empty, the JetStream pull is not using a durable correlation key tied to the request.

## 4. JetStream storage persists in a shared temp directory across test runs

The embedded NATS server is configured with:

- `StoreDir: os.TempDir()`

in:

- [`nats.go:83`](/home/zdm2/Documents/github/FAlCon/falcon-instrument-hub/runtime/internal/networking/nats.go#L83)

Relevant lines:

- [`nats.go:84`](/home/zdm2/Documents/github/FAlCon/falcon-instrument-hub/runtime/internal/networking/nats.go#L84)
- [`nats.go:88`](/home/zdm2/Documents/github/FAlCon/falcon-instrument-hub/runtime/internal/networking/nats.go#L88)

This increases the chance that JetStream-backed measurement data survives across test processes long enough to be fetched by a later test, especially since the measure stream is configured with a `MaxAge` of 60 seconds in the handler.

## 5. The test harness already shows signs of cross-test residue

In the new log, the second test startup includes:

- `daemon stopped`

before starting a new `instrument-script-server` daemon.

That suggests the previous test left background state behind and the next test had to clean it up during startup rather than inheriting a perfectly clean environment.

The fixture teardown does wait on the hub process:

- [`data-retrieval.cpp:225`](/home/zdm2/Documents/github/FAlCon/instrument-controller/tests/instrument-control/data-retrieval.cpp#L225)

but the logs indicate that test isolation is still not fully clean at the system level.

## Most Likely Failure Chain

Most likely sequence:

1. `SetVoltage` publishes a valid one-point `dc_voltage_source` response.
2. Some combination of:
   - delayed background shutdown
   - reused embedded NATS / JetStream state
   - non-correlated `FALCON.MEASURE_RESPONSE`
   leaves that response observable by the next test.
3. `Gaussian1DMeasureGetSet` subscribes to `FALCON.MEASURE_RESPONSE`.
4. `RoutineComms` accepts the first response message it sees, without checking the hash.
5. The controller then pulls measurement data using an under-specified `Channel` value.
6. The test receives the stale `SetVoltage` payload and fails with:
   - `dc_voltage_source`
   - single-point array

## Confidence Assessment

High confidence:

- The failing payload shape matches `SetVoltage`, not Gaussian.
- `RoutineComms` does not correlate responses by hash or timestamp.
- The hub does not populate `Channel`.
- JetStream storage is shared through `os.TempDir()`.

Medium confidence:

- The exact stale source is either:
  - a delayed prior runtime / daemon message, or
  - persistent JetStream state reused across test processes, or
  - both.

Lower confidence:

- Whether the stale `FALCON.MEASURE_RESPONSE` notification itself came from a prior hub shutdown race, or from a still-running helper process.

## Recommended Fix Order

### Short-term protocol hardening

1. Filter `FALCON.MEASURE_RESPONSE` in `RoutineComms::subscribe_measure_response(...)` by hash.
2. Also reject responses whose timestamp/hash do not match the current request.
3. Populate `Channel` in the hub `MeasureResponse` and use it as a real correlation handle.

### Test isolation hardening

1. Give each embedded NATS test run its own unique JetStream `StoreDir`.
2. Consider clearing the temporary store on startup or teardown for integration tests.
3. Verify all spawned hub/ISS child processes are fully gone before the next test starts.

### Extra diagnostics if we want proof before code changes

1. Log the request hash on the C++ side before publishing `MEASURE_COMMAND`.
2. Log the received `MeasureResponse.Hash` in `RoutineComms`.
3. Log the `Stream` and `Channel` values being returned from the hub.
4. Log the `measureSubject` in the hub for every measurement path.

## Bottom Line

The most likely explanation is **cross-test message contamination caused by weak response correlation**, not a bad Gaussian script.

The biggest protocol smells are:

- first-message-wins subscription logic in C++
- missing `Channel` population in the hub
- shared JetStream store directory across test runs

That combination is enough to explain why re-enabling `Gaussian1DMeasureGetSet` makes it appear to receive the earlier `SetVoltage` data.
