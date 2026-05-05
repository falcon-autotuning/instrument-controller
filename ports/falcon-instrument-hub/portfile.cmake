vcpkg_from_github(
       OUT_SOURCE_PATH SOURCE_PATH
       REPO falcon-autotuning/falcon-instrument-hub
       REF v${VERSION}
       SHA512 9b3eec2baa3ebfb7481ccc782223653f54f884e35443b3e7f316f728da81162ef4e67d1b913dbfe1b76be5e1b1df5432ec5807b31a712ba993a3e6565b88f9ef
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
