get_filename_component(WORKSPACE_ROOT "${CURRENT_PORT_DIR}/../../.." ABSOLUTE)
set(LOCAL_SCRIPT_SERVER_PATH "${WORKSPACE_ROOT}/instrument-script-server")
if(EXISTS "${LOCAL_SCRIPT_SERVER_PATH}")
  set(SOURCE_PATH "${LOCAL_SCRIPT_SERVER_PATH}")
else()
  vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO falcon-autotuning/instrument-script-server
        REF v${VERSION}
        SHA512 0bc8141bb07540e5180d19e8571a6ab8200c9757f28f5bc0443c111dcdec09182fe821fe46374709a5b1a7a3f1a58403062efaec6c26de0028fd71b6ddb78f32
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
