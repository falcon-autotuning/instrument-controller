get_filename_component(WORKSPACE_ROOT "${CURRENT_PORT_DIR}/../../.." ABSOLUTE)
set(LOCAL_SCRIPT_SERVER_PATH "${WORKSPACE_ROOT}/instrument-script-server")
if(EXISTS "${LOCAL_SCRIPT_SERVER_PATH}")
  set(SOURCE_PATH "${LOCAL_SCRIPT_SERVER_PATH}")
else()
  vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO falcon-autotuning/instrument-script-server
        REF v${VERSION}
        SHA512 4b7f70a7244004a28bbfc766eed8670988c9e808f49bfb4ae8d7640d1d42815fdf4bc65b19bec44e2f1c5b130e17393482602c69bac3db901d3f4ea07bc7c1f8  
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
