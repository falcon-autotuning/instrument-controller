message(STATUS "falcon-instrument-hub: using GitHub source v${VERSION}")
vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO falcon-autotuning/falcon-instrument-hub
        REF v${VERSION}
        SHA512 ac2e0e99295bb9d813b3e4ee4c21b1074613e977b94b2e46a2a9d71ab9eb686f0dcef3cd9e0ee84fb34a60fba8478acb130585464885b176279657a5d8f3d263
    )

# ------------------------------------------------------------------------------
# Go build outputs
# ------------------------------------------------------------------------------
if(VCPKG_TARGET_IS_WINDOWS)
  set(GO_OUTPUT "${CURRENT_PACKAGES_DIR}/bin/instrument-hub.exe")
  set(DATAVIEWER_OUTPUT "${CURRENT_PACKAGES_DIR}/bin/dataviewer.exe")
else()
  set(GO_OUTPUT "${CURRENT_PACKAGES_DIR}/bin/instrument-hub")
  set(DATAVIEWER_OUTPUT "${CURRENT_PACKAGES_DIR}/bin/dataviewer")
endif()

file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/bin")

# ------------------------------------------------------------------------------
# CGO / linking setup
# ------------------------------------------------------------------------------
set(ENV{PKG_CONFIG_PATH} "${CURRENT_INSTALLED_DIR}/lib/pkgconfig")
set(FALCON_VCPKG_LIB_DIR "${CURRENT_INSTALLED_DIR}/lib")

# Normalize path for Go (important for Windows!)
file(TO_CMAKE_PATH "${FALCON_VCPKG_LIB_DIR}" FALCON_VCPKG_LIB_DIR_NORM)

set(FALCON_GO_CGO_LDFLAGS
    "-L${FALCON_VCPKG_LIB_DIR_NORM} -Wl,-rpath-link,${FALCON_VCPKG_LIB_DIR_NORM}"
)

# ------------------------------------------------------------------------------
# Go module normalization
# ------------------------------------------------------------------------------
# Release/package builds should not depend on a checked-in local replace path.
# Drop it first if it exists; `go mod edit -dropreplace` is a no-op when absent.
vcpkg_execute_required_process(
  COMMAND go mod edit
    "-dropreplace=github.com/falcon-autotuning/falcon-core-libs/go/falcon-core"
  WORKING_DIRECTORY "${SOURCE_PATH}/runtime"
  LOGNAME go-mod-dropreplace
)

# The v1.0.20 hub release tarball still references falcon-core Go module
# v0.0.3, but the published submodule tag is now v0.0.4. Normalize the
# extracted source so fresh vcpkg buildtrees resolve the public module tag.
vcpkg_execute_required_process(
  COMMAND go mod edit
    "-require=github.com/falcon-autotuning/falcon-core-libs/go/falcon-core@v0.0.4"
  WORKING_DIRECTORY "${SOURCE_PATH}/runtime"
  LOGNAME go-mod-require
)

# ------------------------------------------------------------------------------
# Prepare Go module metadata
# ------------------------------------------------------------------------------
# Newer hub releases rely on the public falcon-core Go submodule tag and need
# go.sum entries materialized before `go build` runs in a fresh vcpkg buildtree.
vcpkg_execute_required_process(
  COMMAND go mod tidy
  WORKING_DIRECTORY "${SOURCE_PATH}/runtime"
  LOGNAME go-mod-tidy
)

# ------------------------------------------------------------------------------
# Build Go binaries
# ------------------------------------------------------------------------------# Check if the environment variable TMPDIR is set and not empty
if(DEFINED ENV{TMPDIR} AND NOT "$ENV{TMPDIR}" STREQUAL "")
  set(GOLANG_TMP_DIR "$ENV{TMPDIR}")
else()
  set(GOLANG_TMP_DIR "/tmp")
endif()
vcpkg_execute_required_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "CC=${CMAKE_C_COMPILER}"
        "GOTMPDIR=${GOLAND_TMP_DIR}"
        "CGO_ENABLED=1"
        "PKG_CONFIG_PATH=${CURRENT_INSTALLED_DIR}/lib/pkgconfig"
        "CGO_LDFLAGS=${FALCON_GO_CGO_LDFLAGS}"
        go build -tags cgo,falcon_core -o "${GO_OUTPUT}" ./cmd/main.go
    WORKING_DIRECTORY "${SOURCE_PATH}/runtime"
    LOGNAME build-go
)

# ------------------------------------------------------------------------------
# License install
# ------------------------------------------------------------------------------
file(INSTALL "${SOURCE_PATH}/LICENSE"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
     RENAME copyright)
