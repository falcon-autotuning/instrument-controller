# QArray Charge-Tuning Demo Changes

## Scope

This document records changes present in the sibling
`qarray-charge-tuning-demo` repository. These changes modernize the standalone
QArray simulator demo and are separate from instrument-controller's hub/ISS
integration tests.

The instrument-controller and hub changes are documented in
[INSTRUMENT_CONTROLLER_HUB_CHANGES.md](INSTRUMENT_CONTROLLER_HUB_CHANGES.md).

## Build-System Migration

The demo is being converted from an environment that assumed preinstalled
libraries under `/opt/falcon` into a self-contained CMake and vcpkg project.

Added project infrastructure includes:

- `CMakeLists.txt`;
- `CMakePresets.json`;
- `vcpkg.json`;
- project overlay ports under `ports/`;
- the `cmake/bootstrap` submodule;
- generated runtime environment support in `cmake/env.sh.in`;
- FFI verification scripts under `cmake/`; and
- a substantially expanded Makefile.

The Makefile now provides:

```text
make venv-setup
make vcpkg-bootstrap
make configure
make build
make test
make install
make clean
```

It also retains Docker Compose targets for the PostgreSQL database used by the
Falcon database package.

## Local Dependency Isolation

The demo now builds dependencies into its own `vcpkg_installed` tree instead of
requiring:

- a system-wide `/opt/falcon` installation;
- manually configured include and library paths; or
- locally built sibling repositories.

The vcpkg manifest declares the Falcon, QArray, plotting, Python-binding, YAML,
logging, and tensor dependencies required by the wrapper and DSL demo. An
optional `routine` feature adds falcon-routine and falcon-comms for variants that
communicate with an instrument hub.

The bootstrap flow also accounts for:

- optional NuGet binary-cache credentials;
- systems where `/tmp` is mounted `noexec`;
- overlay ports and overlay triplets;
- Clang C++20 module scanning requirements.

## Python and QArray Environment

The demo now owns a Python virtual environment created through `uv`.
`requirements.txt` installs:

```text
qarray>=1.6.0
numpy>=1.24.0
pyyaml>=6.0
```

Python 3.12 is selected because the QArray Rust core does not currently provide
the expected Python 3.13 wheel in this build flow.

The build pins all three Python components to the same virtual environment:

- interpreter;
- headers;
- shared `libpython` library.

This avoids pairing the virtual-environment interpreter with headers or a
library from vcpkg's separate Python installation.

## QArray FFI Wrapper

`qarray-wrapper.cpp` was updated for the current installed header layout:

```text
falcon_core/...        -> falcon-core/...
qarrayDevice/...       -> falcon-qarray-device/...
```

Python initialization no longer hard-codes:

```text
/opt/falcon/lib/libpython3.12.so
```

Instead, the build supplies the matching `libpython` path through
`FALCON_LIBPYTHON_PATH`, with an optional runtime override through
`FALCON_LIBPYTHON`. The library is loaded with `RTLD_GLOBAL` before starting the
embedded interpreter so NumPy and QArray extension modules can resolve CPython
symbols.

The wrapper is also built as a normal CMake shared library. That build catches
header and link regressions before the Falcon DSL toolchain attempts to compile
the same translation unit through `ffimport`.

## Generated Falcon FFI Definition

The manually maintained `hub.fal` has been replaced by `hub.fal.in` as the
source of truth. CMake generates `hub.fal` with paths derived from:

- the selected vcpkg tree;
- the selected Python include directory;
- the selected Python library directory; and
- the exact Python link-library name.

The generated `ffimport` block links against the project-local Falcon, QArray,
PLplot, YAML, logging, threading, dynamic-loading, and Python libraries.

This removes path drift between the CMake wrapper build and the wrapper build
performed by the Falcon package manager.

## Falcon DSL Import Migration

Standard-library imports in the demo's `.fal` files were changed from repository
relative paths such as:

```text
../../libs/database/database.fal
```

to package-search paths such as:

```text
libs/database/database.fal
```

This applies to:

- `chargeConfigurationTuner.fal`;
- `stateStepper.fal`;
- `tests/run_tests.fal`; and
- the generated `hub.fal` definition.

Local demo imports, such as `./stateStepper.fal` and `../hub.fal`, remain
relative. Standard-library imports are resolved through `FALCON_LIBRARY_PATH`,
which points to the Falcon library packages installed in the local vcpkg tree.

The demo manifest no longer uses old `local_path` dependencies for individual
Falcon library files. It instead records repository metadata and the generated
FFI artifact.

## Runtime Environment

CMake generates `build/<preset>/env.sh`. Sourcing it configures:

- `LD_LIBRARY_PATH` for vcpkg, Python, and the wrapper;
- `FALCON_QARRAY_PYTHON`;
- `FALCON_QARRAY_PYTHON_PATH` for the installed `device.py`;
- `PLPLOT_LIB` for plot data;
- `QARRAY_CONFIG_PATH` for `2_dot.yml`;
- `FALCON_DATABASE_URL` for the demo database;
- `PATH` for `falcon-run` and `falcon-test`;
- `CXX` for the compiler used by Falcon `ffimport`; and
- `FALCON_LIBRARY_PATH` for installed Falcon DSL packages.

The test paths in `tests/run_tests.fal` were also adjusted so `2_dot.yml` is
resolved from the demo's configured execution context.

## Demo Verification

Two build-time checks were added:

1. `qarray-wrapper-symbols` verifies that the CMake-built shared library exports
   all eight routines declared by `hub.fal`.
2. `ffimport-flags` recompiles and links `qarray-wrapper.cpp` using only the
   generated `hub.fal` flags, then verifies the same exported symbols.

These checks distinguish a successful CMake transitive link from a genuinely
complete Falcon `ffimport` configuration.

The expected standalone validation flow is:

```bash
cd /home/zdm2/Documents/github/FAlCon/qarray-charge-tuning-demo
make build
make test
make docker-up
. build/linux-clang-release/env.sh
falcon-test ./tests/run_tests.fal --log-level info
```

## Current Status

The files described here are present as worktree changes in the QArray demo
repository. This document does not claim that a complete CMake, FFI, database,
and Falcon DSL run has passed unless those commands have been run after the
latest worktree changes.

Generated binaries, plots, virtual environments, vcpkg build trees, and other
local artifacts should remain excluded from source commits.

