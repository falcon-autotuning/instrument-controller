vcpkg_from_github(
       OUT_SOURCE_PATH SOURCE_PATH
       REPO falcon-autotuning/falcon-instrument-hub
       REF v${VERSION}
       SHA512 f3c4a4fdfcf9af446fa20d5c6b834606c868c83484704c6973fb1b3fe6ea780df00ef6e8b3255b04cf6f4e0ad137005096889d0bea4c03d20de64b04a1da20a8
   )

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

# Fix the go.mod replace directive: the relative path ../../falcon-core-libs/go/falcon-core
# is only valid in the source tree, not in the vcpkg build tree. Rewrite it to the absolute path.
get_filename_component(WORKSPACE_ROOT "${CURRENT_PORT_DIR}/../../.." ABSOLUTE)
set(FALCON_CORE_LIBS_GO_DIR "${WORKSPACE_ROOT}/falcon-core-libs/go/falcon-core")
vcpkg_execute_required_process(
       COMMAND go mod edit "-replace=github.com/falcon-autotuning/falcon-core-libs/go/falcon-core=${FALCON_CORE_LIBS_GO_DIR}"
       WORKING_DIRECTORY "${SOURCE_PATH}/runtime"
       LOGNAME go-mod-edit
   )

# Inject local main.go fix (startInstruments after handlers subscribed)
set(HUB_MAIN_OVERRIDE "${WORKSPACE_ROOT}/falcon-instrument-hub/runtime/cmd/main.go")
if(EXISTS "${HUB_MAIN_OVERRIDE}")
    file(COPY "${HUB_MAIN_OVERRIDE}" DESTINATION "${SOURCE_PATH}/runtime/cmd")
endif()

# Inject local measure_command_handler.go (Phase 4a: NATS subject alignment)
set(HUB_HANDLER_OVERRIDE "${WORKSPACE_ROOT}/falcon-instrument-hub/runtime/internal/handlers/measure_command_handler.go")
if(EXISTS "${HUB_HANDLER_OVERRIDE}")
    file(COPY "${HUB_HANDLER_OVERRIDE}" DESTINATION "${SOURCE_PATH}/runtime/internal/handlers")
endif()

vcpkg_execute_required_process(
  COMMAND "${CMAKE_COMMAND}" -E env "CGO_LDFLAGS=${FALCON_GO_CGO_LDFLAGS}" go build -o "${GO_OUTPUT}" ./cmd/main.go
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
