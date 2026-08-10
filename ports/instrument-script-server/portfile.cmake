get_filename_component(WORKSPACE_ROOT "${CURRENT_PORT_DIR}/../../.." ABSOLUTE)
set(LOCAL_SCRIPT_SERVER_PATH "${WORKSPACE_ROOT}/instrument-script-server")
if(EXISTS "${LOCAL_SCRIPT_SERVER_PATH}")
  set(SOURCE_PATH "${LOCAL_SCRIPT_SERVER_PATH}")
else()
  vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO falcon-autotuning/instrument-script-server
        REF v${VERSION}
        SHA512 c5eec1c9732828275e7581ff4068f53554327aa39d9c09cda1e0f0b4397d14a22746a3b3abcea86a6693bbc6b76d3dd0e278865021440950f0bacc2a2caa4458
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
