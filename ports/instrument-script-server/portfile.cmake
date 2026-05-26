# Use the local workspace source when available, otherwise download from GitHub.
# This avoids needing a new GitHub release every time the ISS changes locally.
get_filename_component(WORKSPACE_ROOT "${CURRENT_PORT_DIR}/../../.." ABSOLUTE)
set(_LOCAL_ISS_DIR "${WORKSPACE_ROOT}/instrument-script-server")
if(EXISTS "${_LOCAL_ISS_DIR}/CMakeLists.txt")
    message(STATUS "instrument-script-server: using local workspace source at ${_LOCAL_ISS_DIR}")
    set(SOURCE_PATH "${_LOCAL_ISS_DIR}")
else()
    message(STATUS "instrument-script-server: local workspace not found, downloading v${VERSION} from GitHub")
    vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO falcon-autotuning/instrument-script-server
        REF v${VERSION}
        SHA512 b303f1ab4d7051b7df81c5f7f80328fd68a457ea17236ea1ed616fd69738dc10470d2d11a5461a3fd798e50de9eda42ac71f40f703aff34b114d24ff6b8f2ac7
    )
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup()

file(INSTALL "${SOURCE_PATH}/LICENSE"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
     RENAME copyright)

vcpkg_copy_pdbs()
