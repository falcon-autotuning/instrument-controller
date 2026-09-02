# Change Documentation Index

The previous combined changelog has been separated by subsystem so that the
instrument-controller/hub integration work is not mixed with the standalone
QArray charge-tuning demo work.

## Instrument Controller and Hub

See [INSTRUMENT_CONTROLLER_HUB_CHANGES.md](INSTRUMENT_CONTROLLER_HUB_CHANGES.md).

This document covers:

- timestamp-based measurement response correlation;
- falcon-comms, falcon-instrument-hub, and falcon-routine release changes;
- instrument-controller vcpkg overlay updates;
- the `data-retrieval.cpp` port-payload refactor;
- Lua/Teal and ISS command-contract migration;
- float32-aware integration-test assertions; and
- integration-test process-lifecycle notes.

## QArray Charge-Tuning Demo

See [QARRAY_DEMO_CHANGES.md](QARRAY_DEMO_CHANGES.md).

This document covers:

- the standalone `qarray-charge-tuning-demo` build system;
- project-local vcpkg and Python environments;
- qarray/CPython embedding and FFI wrapper changes;
- Falcon DSL package import migration;
- generated `hub.fal` and runtime environment files; and
- demo-specific build and FFI verification.

## Scope Boundary

The quantum-dot device and wiremap files used by
`instrument-controller/tests/instrument-control/data-retrieval.cpp` remain in
the instrument-controller/hub document. They exercise the hub integration test
stack and are not part of the standalone QArray simulator demo build.

