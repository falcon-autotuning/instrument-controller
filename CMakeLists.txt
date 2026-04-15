cmake_minimum_required(VERSION 3.20)
project(
  instrument-controller
  VERSION 1.0.0
  DESCRIPTION "Instrument Controller - Full instrument control stack for FAlCon"
  HOMEPAGE_URL "https://github.com/falcon-autotuning/instrument-controller"
  LANGUAGES CXX C
)

# ============================================================================
# CPack General Settings (shared across all platforms)
# ============================================================================
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

# ============================================================================
# Platform-Specific CPack Configuration
# ============================================================================

if(WIN32)
  set(CPACK_GENERATOR "NSIS")
  set(CPACK_NSIS_INSTALL_ROOT "C:\\")
  set(CPACK_NSIS_HELP_LINK "${PROJECT_HOMEPAGE_URL}")
  set(CPACK_NSIS_URL_INFO_ABOUT "${PROJECT_HOMEPAGE_URL}")
  set(CPACK_NSIS_CONTACT "tkovach@wisc.edu")
  set(CPACK_NSIS_CREATE_ICONS_EXTRA "")
  set(CPACK_NSIS_DELETE_ICONS_EXTRA "")
  set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon-128.png")
  set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon-128.png")

else()
  set(CPACK_GENERATOR "TXST")
  set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
  set(CPACK_ARCHIVE_THREADS 0)

endif()

# Installation Directory Structure
set(INSTALL_LIB_DIR "lib")
set(INSTALL_INCLUDE_DIR "include")
set(INSTALL_SHARE_DIR "share")
set(INSTALL_PKGCONFIG_DIR "lib/pkgconfig")
set(INSTALL_DOC_DIR "doc")
if(WIN32)
  set(INSTALL_BIN_DIR "lib")
else()
  set(INSTALL_BIN_DIR "bin")
endif()

# ============================================================================
# Install Targets
# ============================================================================

# Create a dependency information file at configure time
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/CMakeFiles/DependencyInfo.txt.in"
  "${CMAKE_BINARY_DIR}/FALCON_DEPENDENCIES.txt"
  @ONLY
)

# Install dependency information
install(
  FILES "${CMAKE_BINARY_DIR}/FALCON_DEPENDENCIES.txt"
  DESTINATION "."
)

# Install license and readme
install(
  FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
  DESTINATION "${INSTALL_DOC_DIR}"
)

if(WIN32)
  install(
    GLOB_RECURSE
    PROGRAMS "${VCPKG_INSTALLED_DIR}/x64-windows/bin/*.dll"
    DESTINATION "${INSTALL_BIN_DIR}"
  )
  install(
    GLOB_RECURSE
    FILES "${VCPKG_INSTALLED_DIR}/x64-windows/lib/*.lib"
    DESTINATION "${INSTALL_LIB_DIR}"
  )
  install(
    GLOB_RECURSE
    FILES "${VCPKG_INSTALLED_DIR}/x64-windows/include/*"
    DESTINATION "${INSTALL_INCLUDE_DIR}"
  )
  install(
    GLOB_RECURSE
    FILES "${VCPKG_INSTALLED_DIR}/x64-windows/share/*"
    DESTINATION "${INSTALL_SHARE_DIR}"
  )
else()
  install(
    GLOB_RECURSE
    FILES "${VCPKG_INSTALLED_DIR}/x64-linux-dynamic/bin/*"
    DESTINATION "${INSTALL_BIN_DIR}"
  )
  install(
    GLOB_RECURSE
    FILES
      "${VCPKG_INSTALLED_DIR}/x64-linux-dynamic/lib/*.a"
      "${VCPKG_INSTALLED_DIR}/x64-linux-dynamic/lib/*.so*"
      "${VCPKG_INSTALLED_DIR}/x64-linux/lib/*.a"
      "${VCPKG_INSTALLED_DIR}/x64-linux/lib/*.so*"
    DESTINATION "${INSTALL_LIB_DIR}"
  )
  install(
    GLOB_RECURSE
    FILES
      "${VCPKG_INSTALLED_DIR}/x64-linux-dynamic/include/*"
      "${VCPKG_INSTALLED_DIR}/x64-linux/include/*"
    DESTINATION "${INSTALL_INCLUDE_DIR}"
  )
  install(
    GLOB_RECURSE
    FILES "${VCPKG_INSTALLED_DIR}/x64-linux-dynamic/share/*"
    DESTINATION "${INSTALL_SHARE_DIR}"
  )
endif()

include(CPack)
