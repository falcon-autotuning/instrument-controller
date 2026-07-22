# Use the local workspace source when available, otherwise download from GitHub.
# This avoids needing a new GitHub release every time the ISS changes locally.
get_filename_component(WORKSPACE_ROOT "${CURRENT_PORT_DIR}/../../.." ABSOLUTE)
set(_LOCAL_ISS_DIR "${WORKSPACE_ROOT}/instrument-script-server")
set(_LOCAL_ISS_CMAKELISTS "${_LOCAL_ISS_DIR}/CMakeLists.txt")

if(EXISTS "${_LOCAL_ISS_CMAKELISTS}")
  message(STATUS "instrument-script-server: using local workspace source at ${_LOCAL_ISS_DIR}")
  set(SOURCE_PATH "${_LOCAL_ISS_DIR}")
else()
  message(STATUS "instrument-script-server: local workspace not found, downloading v${VERSION} from GitHub")
  vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO falcon-autotuning/instrument-script-server
        REF v${VERSION}
        SHA512 e55d07a6cc09d49cbd2c666a2e1812dc0109a74c9a25bff098176f976aea7be2ba5039d15e89f152d2f482e5e2b40e7a2201f1554899b0f1275ef2be87b129b3
  )
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_CLI=ON
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup()

file(INSTALL "${SOURCE_PATH}/LICENSE"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
     RENAME copyright)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/share/doc/instrument-script-server/assets/icons")

vcpkg_copy_pdbs()
