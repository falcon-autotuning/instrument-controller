# Use the local workspace source when available, otherwise download from GitHub.
# This avoids needing a new GitHub release every time the hub changes locally.
get_filename_component(WORKSPACE_ROOT "${CURRENT_PORT_DIR}/../../.." ABSOLUTE)
set(_LOCAL_HUB_DIR "${WORKSPACE_ROOT}/falcon-instrument-hub")
if(EXISTS "${_LOCAL_HUB_DIR}/runtime/cmd/main.go")
    message(STATUS "falcon-instrument-hub: using local workspace source at ${_LOCAL_HUB_DIR}")
    set(SOURCE_PATH "${_LOCAL_HUB_DIR}")
else()
    message(STATUS "falcon-instrument-hub: local workspace not found, downloading v${VERSION} from GitHub")
    vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO falcon-autotuning/falcon-instrument-hub
        REF v${VERSION}
        SHA512 38b3a42d56bfd37c9758281675be6c2ee70d070bb8d709a6959fb3f9f50275e0f422bc7b537a15b49a4a7f80944dab839e85282d326574726ef7e006b22d2854
    )
endif()

# Build the Go binary
if(VCPKG_TARGET_IS_WINDOWS)
  set(GO_OUTPUT "${CURRENT_PACKAGES_DIR}/bin/instrument-hub.exe")
  set(DATAVIEWER_OUTPUT "${CURRENT_PACKAGES_DIR}/bin/dataviewer.exe")
else()
  set(GO_OUTPUT "${CURRENT_PACKAGES_DIR}/bin/instrument-hub")
  set(DATAVIEWER_OUTPUT "${CURRENT_PACKAGES_DIR}/bin/dataviewer")
endif()

file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/bin")

set(ENV{PKG_CONFIG_PATH} "${CURRENT_INSTALLED_DIR}/lib/pkgconfig")
set(FALCON_VCPKG_LIB_DIR "${CURRENT_INSTALLED_DIR}/lib")
set(FALCON_GO_CGO_LDFLAGS "-L${FALCON_VCPKG_LIB_DIR} -Wl,-rpath-link,${FALCON_VCPKG_LIB_DIR}")

# Fix the go.mod replace directive so the build finds falcon-core-libs.
# When building from the local workspace the existing relative path already
# resolves correctly, so skip the edit to avoid dirtying go.mod in git.
# When building from the GitHub download the relative path is invalid, so
# rewrite it to an absolute path.
set(FALCON_CORE_LIBS_GO_DIR "${WORKSPACE_ROOT}/falcon-core-libs/go/falcon-core")
if(NOT SOURCE_PATH STREQUAL "${_LOCAL_HUB_DIR}")
    vcpkg_execute_required_process(
           COMMAND go mod edit "-replace=github.com/falcon-autotuning/falcon-core-libs/go/falcon-core=${FALCON_CORE_LIBS_GO_DIR}"
           WORKING_DIRECTORY "${SOURCE_PATH}/runtime"
           LOGNAME go-mod-edit
       )
endif()

# When building from GitHub download, inject any local overrides from the workspace.
if(NOT SOURCE_PATH STREQUAL "${_LOCAL_HUB_DIR}")
    # Inject local main.go fix (startInstruments after handlers subscribed)
    set(HUB_MAIN_OVERRIDE "${WORKSPACE_ROOT}/falcon-instrument-hub/runtime/cmd/main.go")
    if(EXISTS "${HUB_MAIN_OVERRIDE}")
        file(COPY "${HUB_MAIN_OVERRIDE}" DESTINATION "${SOURCE_PATH}/runtime/cmd")
    endif()

    # Inject all local handlers/ overrides
    set(HUB_HANDLERS_DIR "${WORKSPACE_ROOT}/falcon-instrument-hub/runtime/internal/handlers")
    if(EXISTS "${HUB_HANDLERS_DIR}")
        file(GLOB HUB_HANDLERS_FILES "${HUB_HANDLERS_DIR}/*.go")
        file(COPY ${HUB_HANDLERS_FILES} DESTINATION "${SOURCE_PATH}/runtime/internal/handlers")
    endif()

    # Inject all local serverinterpreter/ overrides
    set(HUB_INTERP_DIR "${WORKSPACE_ROOT}/falcon-instrument-hub/runtime/internal/serverinterpreter")
    if(EXISTS "${HUB_INTERP_DIR}")
        file(GLOB HUB_INTERP_FILES "${HUB_INTERP_DIR}/*.go")
        file(COPY ${HUB_INTERP_FILES} DESTINATION "${SOURCE_PATH}/runtime/internal/serverinterpreter")
    endif()
endif()

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
  COMMAND "${CMAKE_COMMAND}" -E env "CGO_LDFLAGS=${FALCON_GO_CGO_LDFLAGS}" go build -o "${DATAVIEWER_OUTPUT}" ./cmd/dataviewer/
       WORKING_DIRECTORY "${SOURCE_PATH}/runtime"
       LOGNAME build-go
   )
file(INSTALL "${SOURCE_PATH}/LICENSE"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
     RENAME copyright
)
