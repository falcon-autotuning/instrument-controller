# ------------------------------------------------------------------------------
# Resolve workspace (optional dev override)
# ------------------------------------------------------------------------------
get_filename_component(WORKSPACE_ROOT "${CURRENT_PORT_DIR}/../../.." ABSOLUTE)

set(_LOCAL_HUB_DIR "${WORKSPACE_ROOT}/falcon-instrument-hub")
set(_LOCAL_CORE_LIBS_DIR "${WORKSPACE_ROOT}/falcon-core-libs/go/falcon-core")

set(USE_LOCAL_HUB OFF)
set(USE_LOCAL_CORE OFF)
set(FALCON_DEV_MODE_ENABLED OFF)

# Detect local hub (dev mode)
if(EXISTS "${_LOCAL_HUB_DIR}/runtime/cmd/main.go")
  set(USE_LOCAL_HUB ON)
endif()

# Detect local core libs (dev mode)
if(EXISTS "${_LOCAL_CORE_LIBS_DIR}/go.mod")
  set(USE_LOCAL_CORE ON)
endif()

# if(DEFINED ENV{FALCON_DEV_MODE})
#   set(FALCON_DEV_MODE_ENABLED ON)
# endif()

# ------------------------------------------------------------------------------
# Source selection (prefer vcpkg/GitHub unless explicitly overridden)
# ------------------------------------------------------------------------------
if(USE_LOCAL_HUB AND FALCON_DEV_MODE_ENABLED)
  message(STATUS "falcon-instrument-hub: using LOCAL workspace source")
  set(SOURCE_PATH "${_LOCAL_HUB_DIR}")
  set(USING_LOCAL_SOURCE TRUE)
else()
  message(STATUS "falcon-instrument-hub: using GitHub source v${VERSION}")
  vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO falcon-autotuning/falcon-instrument-hub
        REF v${VERSION}
        SHA512 115e33f91c91a87e279ce8234aeab76d2f9e3f2361f4ed33932677bee27719ac71255c4eb45d6236433306c6d97b4803fd44f9d79231d8a2a05c6d81b1ee3fb3
    )
  set(USING_LOCAL_SOURCE FALSE)
endif()

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

if(FALCON_DEV_MODE_ENABLED AND USE_LOCAL_CORE)
  message(STATUS "Using LOCAL falcon-core-libs override via go.mod replace")

  # Normalize path for Go (important for Windows)
  file(TO_CMAKE_PATH "${_LOCAL_CORE_LIBS_DIR}" LOCAL_CORE_LIBS_GO_DIR_NORM)

  vcpkg_execute_required_process(
    COMMAND go mod edit
      "-replace=github.com/falcon-autotuning/falcon-core-libs/go/falcon-core=${LOCAL_CORE_LIBS_GO_DIR_NORM}"
    WORKING_DIRECTORY "${SOURCE_PATH}/runtime"
    LOGNAME go-mod-edit
  )
endif()

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
# Optional dev overrides (only when explicitly enabled)
# ------------------------------------------------------------------------------
if(FALCON_DEV_MODE_ENABLED AND USE_LOCAL_HUB)
  message(STATUS "Injecting local dev overrides into hub")

  set(HUB_MAIN_OVERRIDE "${_LOCAL_HUB_DIR}/runtime/cmd/main.go")
  if(EXISTS "${HUB_MAIN_OVERRIDE}")
    file(COPY "${HUB_MAIN_OVERRIDE}"
             DESTINATION "${SOURCE_PATH}/runtime/cmd")
  endif()

  set(HUB_HANDLERS_DIR
        "${_LOCAL_HUB_DIR}/runtime/internal/handlers")
  if(EXISTS "${HUB_HANDLERS_DIR}")
    file(GLOB HUB_HANDLERS_FILES "${HUB_HANDLERS_DIR}/*.go")
    file(COPY ${HUB_HANDLERS_FILES}
             DESTINATION "${SOURCE_PATH}/runtime/internal/handlers")
  endif()

  set(HUB_INTERP_DIR
        "${_LOCAL_HUB_DIR}/runtime/internal/serverinterpreter")
  if(EXISTS "${HUB_INTERP_DIR}")
    file(GLOB HUB_INTERP_FILES "${HUB_INTERP_DIR}/*.go")
    file(COPY ${HUB_INTERP_FILES}
             DESTINATION "${SOURCE_PATH}/runtime/internal/serverinterpreter")
  endif()
endif()

# ------------------------------------------------------------------------------
# Build Go binaries
# ------------------------------------------------------------------------------
vcpkg_execute_required_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "CGO_ENABLED=1"
        "PKG_CONFIG_PATH=${CURRENT_INSTALLED_DIR}/lib/pkgconfig"
        "CGO_LDFLAGS=${FALCON_GO_CGO_LDFLAGS}"
        go build -tags cgo,falcon_core -o "${GO_OUTPUT}" ./cmd/main.go
    WORKING_DIRECTORY "${SOURCE_PATH}/runtime"
    LOGNAME build-go
)

vcpkg_execute_required_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "CGO_LDFLAGS=${FALCON_GO_CGO_LDFLAGS}"
        go build -o "${DATAVIEWER_OUTPUT}" ./cmd/dataviewer/
    WORKING_DIRECTORY "${SOURCE_PATH}/runtime"
    LOGNAME build-go
)

# ------------------------------------------------------------------------------
# License install
# ------------------------------------------------------------------------------
file(INSTALL "${SOURCE_PATH}/LICENSE"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
     RENAME copyright)
