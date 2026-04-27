vcpkg_from_github(
       OUT_SOURCE_PATH SOURCE_PATH
       REPO falcon-autotuning/falcon-instrument-hub
       REF master
       SHA512 0
   )

# Build the Go binary
if(VCPKG_TARGET_IS_WINDOWS)
  set(GO_OUTPUT "${CURRENT_PACKAGES_DIR}/bin/instrument-server.exe")
  set(DATAVIEWER_OUTPUT "${CURRENT_PACKAGES_DIR}/bin/dataviewer.exe")
else()
  set(GO_OUTPUT "${CURRENT_PACKAGES_DIR}/bin/instrument-server")
  set(DATAVIEWER_OUTPUT "${CURRENT_PACKAGES_DIR}/bin/dataviewer")
endif()

file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/bin")

vcpkg_execute_required_process(
       COMMAND go build -o "${GO_OUTPUT}" ./cmd/main.go
       WORKING_DIRECTORY "${SOURCE_PATH}/runtime"
       LOGNAME build-go
   )
vcpkg_execute_required_process(
       COMMAND go build -o "${GO_OUTPUT}" ./cmd/dataviewer/
       WORKING_DIRECTORY "${SOURCE_PATH}/runtime"
       LOGNAME build-go
   )
