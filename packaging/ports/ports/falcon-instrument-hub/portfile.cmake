vcpkg_from_github(
       OUT_SOURCE_PATH SOURCE_PATH
       REPO falcon-autotuning/falcon-instrument-hub
       REF main
       SHA512 b626a2de69e20b36cb6fdad1bcffcb8df8173f4f33745b8fdcf8017b82cb635b46fd0aa519ecb21add6125cb5b3c85d72449fed2226c72ed3ec025ca00d1aef1
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
       COMMAND go build -o "${DATAVIEWER_OUTPUT}" ./cmd/dataviewer/
       WORKING_DIRECTORY "${SOURCE_PATH}/runtime"
       LOGNAME build-go
   )
file(INSTALL "${SOURCE_PATH}/LICENSE"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
     RENAME copyright
)
