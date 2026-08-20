get_filename_component(WORKSPACE_ROOT "${CURRENT_PORT_DIR}/../../.." ABSOLUTE)
set(LOCAL_SCRIPT_SERVER_PATH "${WORKSPACE_ROOT}/instrument-script-server")
if(EXISTS "${LOCAL_SCRIPT_SERVER_PATH}")
  set(SOURCE_PATH "${LOCAL_SCRIPT_SERVER_PATH}")
else()
  vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO falcon-autotuning/instrument-script-server
        REF v${VERSION}
        SHA512 47210ed61378046ffa3717fb8127af265d12d02ce869dced3e43f73abaeaf1615baac344ad47ad0576fea2f6bcc144e27248c27726366a3145ae27098a9850c1
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
