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
        SHA512 4f7f83b19118d1c046dede416a21ecd0073d92bdb9da5ac931d77ebeaa9e95446eb6a10bb9b9734587f1713afa3812fbe6365f28fa4b918329c692663dec15cf    
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
