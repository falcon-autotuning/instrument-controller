get_filename_component(WORKSPACE_ROOT "${CURRENT_PORT_DIR}/../../.." ABSOLUTE)
set(LOCAL_SCRIPT_SERVER_PATH "${WORKSPACE_ROOT}/instrument-script-server")
if(EXISTS "${LOCAL_SCRIPT_SERVER_PATH}")
  set(SOURCE_PATH "${LOCAL_SCRIPT_SERVER_PATH}")
else()
  vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO falcon-autotuning/instrument-script-server
        REF v${VERSION}
        SHA512 a8164fcba4fec125b74b8262aa62a15ce4fd75e8d333e3ff3697f367f08f6f61272f3b85e9315716f59cd828d0b49afa0a245d20b5c01440a5b3454bc3f94879
  )
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_CLI=ON
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME instrument-script-server CONFIG_PATH share/instrument-script-server)
vcpkg_copy_tools(TOOL_NAMES instrument-script-server instrument-script-server-daemon instrument-worker AUTO_CLEAN)



file(INSTALL "${SOURCE_PATH}/LICENSE"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
     RENAME copyright)

vcpkg_copy_pdbs()
