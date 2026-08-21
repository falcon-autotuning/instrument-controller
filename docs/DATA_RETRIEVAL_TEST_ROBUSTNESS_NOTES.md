# Data Retrieval Test Robustness Notes

Date: 2026-07-22

This note records follow-up improvements for
`tests/instrument-control/data-retrieval.cpp` after the hub/ISS
gRPC/protobuf refactor reached a green controller test run.

Current passing anchor:

```text
tests/hub/log/make_test_out.txt
100% tests passed, 0 tests failed out of 20
```

The tests are now useful integration smoke tests for the full controller to hub
to ISS path, but several areas should be strengthened before treating them as
robust behavioral coverage.

## Current Strengths

- The suite validates the end-to-end process lifecycle: controller test harness,
  hub startup, embedded NATS, ISS daemon startup, instrument worker startup,
  Lua script execution, plugin command dispatch, result conversion, and NATS
  response publication.
- The tests cover scalar setters, scalar getters, multi-target set/get flows,
  measurement scripts, and buffered 1D/2D measurement scripts.
- Each test gets a unique run directory under `tests/test-runs/`, which makes
  debugging much easier and reduces log/data collisions.
- The fixture waits for NATS and the hub status signal before running each test,
  which is important after the hub moved to gRPC startup/readiness checks.

## Recommended Test Improvements

### Assert Buffered Data Contents

The buffered tests currently verify response envelope metadata and array sizes,
but they do not verify the actual numeric data returned from ISS buffers.

Relevant tests:

- `Gaussian1DMeasureGetSet`
- `VoltageSweepCurrent`
- `VoltageSweepCurrent2D`

Recommended improvement:

- Assert representative values from the returned arrays, such as first, second,
  last, and/or a small sample across the array.
- Compare against `tests/test-data/gaussian-1d.txt` or
  `tests/test-data/linear-1d.txt` as appropriate.
- For 2D data, verify shape-derived indexing rather than only total size.

Why this matters:

- The gRPC/protobuf path includes `MeasureJobResult` plus `ReadBuffer`.
  A test that only checks buffer length can pass even if buffer values are
  wrong, reordered, zero-filled, or read from the wrong channel.

### Assert Priming Requests

Several getter tests first issue a setter/priming measurement, but discard the
response:

```cpp
(void)request_measurement(set_request, TIMEOUT_MS);
```

Recommended improvement:

- Capture and assert the priming response before issuing the getter request.
- Use `ExpectSinglePointEchoResponse(...)` or
  `ExpectMultiTargetEchoResponse(...)` where applicable.

Why this matters:

- If priming silently fails, the later getter failure may be misleading.
- If the hub returns a response but the plugin command failed internally, the
  test should fail at the priming step with a clearer cause.

### Strengthen Setter-Only Tests With Readback

Some setter-only tests primarily assert the controller-facing response emitted
by the hub. That is useful, but it does not always prove that plugin state
changed.

Recommended improvement:

- For setters that have a matching getter, add a readback step in the same test
  or in a paired test.
- For settings such as sample rate and bin count, verify that the corresponding
  getter returns the applied value.

Why this matters:

- During the refactor, commands could reach the worker and still return plugin
  errors. A high-level response alone was not enough to prove the underlying
  instrument command succeeded.

### Replace `std::exit(1)` In Test Helpers

Several setup helpers call `std::exit(1)` after tool failures:

- `GenerateTealInstrumentLibs(...)`
- `BuildTestData(...)`
- `RunTestDataGenerator(...)`
- `ExtendInstrumentApis(...)`
- `CompileTeal(...)`

Recommended improvement:

- Replace hard process exits with GoogleTest assertions or exceptions that fail
  the current test cleanly.
- Include the command that failed and the exit code in the assertion message.

Why this matters:

- `std::exit(1)` aborts the entire test binary immediately.
- A normal test failure preserves teardown behavior, structured reporting, and
  often more useful logs.

### Avoid Fixed Ports Or Add Explicit Resource Locking

The test fixture currently uses fixed ports:

- NATS: `4222`
- ISS gRPC daemon: `5555`

Recommended improvement:

- Either allocate per-test free ports, or add a CTest resource lock that
  prevents parallel execution of these tests.
- If fixed ports remain, add an early diagnostic that reports which process is
  already bound when startup fails.

Why this matters:

- Fixed ports are acceptable for sequential local runs, but fragile under
  parallel CTest, repeated interrupted runs, or stale local processes.

### Add Cleanup Assertions

The fixture calls `StopInstrumentHub()` in `TearDown()`, and the hub is expected
to stop ISS and instrument workers. However, after `make test` finishes, a
lingering `instrument-worker` and `instrument-script-server` daemon have been
observed in the background.

Recommended improvement:

- After stopping the hub, assert that the hub child process exited.
- Add a bounded wait for ISS daemon shutdown.
- Add a bounded wait for all worker processes started by the test to exit.
- When cleanup fails, print:
  - process id
  - parent process id
  - command line
  - current test run directory
  - relevant log file paths

Useful diagnostic command:

```sh
ps -eo pid,ppid,stat,cmd | rg 'instrument-script-server|instrument-worker|instrument-hub'
```

At the time this note was written, that command did not show matching processes,
but the post-`make test` lingering-process observation should still be treated
as an important cleanup bug if it reproduces.

Possible cleanup failure points:

- The hub may receive `SIGTERM` but not complete its ISS shutdown path before
  the test exits.
- The hub may stop ISS but ISS may leave worker children alive.
- The ISS daemon shutdown command may fail or return before workers are fully
  reaped.
- A stale daemon PID file or shutdown pipe may cause a later test to talk to, or
  fail around, the wrong daemon instance.
- `StopInstrumentHub()` waits for the hub process, but does not directly verify
  that the daemon and worker descendants exited.

## Suggested Priority

1. Add post-test process cleanup checks for ISS and workers.
2. Assert priming responses instead of discarding them.
3. Verify buffered array contents, not only shape and metadata.
4. Replace setup-time `std::exit(1)` calls with clean GoogleTest failures.
5. Add dynamic ports or CTest resource locking.

The current suite is good enough to prove that the refactored gRPC/protobuf
stack can work end to end. These improvements would make it better at catching
wrong-data bugs, partial command failures, and lifecycle leaks.
