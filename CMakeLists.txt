cmake_minimum_required(VERSION 3.20)
project(
  instrument-controller
  VERSION 1.0.0
  DESCRIPTION "Instrument Controller - Full instrument control stack for Falcon"
  HOMEPAGE_URL "https://github.com/falcon-autotuning/instrument-controller"
  LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build Options
option(BUILD_TESTS "Build tests" OFF)
option(BUILD_EXAMPLES "Build example plugins" ON)

# Performance options
option(USE_CCACHE "Enable ccache or sccache if available" ON)
option(ENABLE_PCH
       "Enable target_precompile_headers to speed up builds (opt-in)" ON)

# Windows-specific definitions
if(WIN32)
  add_compile_definitions(NOMINMAX)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# Try to auto-detect a compiler launcher (prefer sccache if present)
if(USE_CCACHE)
  find_program(SCCACHE_PROGRAM sccache)
  if(SCCACHE_PROGRAM)
    message(STATUS "Using sccache: ${SCCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER ${SCCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${SCCACHE_PROGRAM})
  else()
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
      message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
      set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
      set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    else()
      message(
        STATUS "No ccache/sccache found (fine, builds will just be normal).")
    endif()
  endif()
endif()

find_package(falcon-core              CONFIG REQUIRED)
find_package(instrument-script-server CONFIG REQUIRED)
find_package(teal-api-gen             CONFIG REQUIRED)

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
# ============================================================================
# Installation Prefix Configuration
# ============================================================================
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  if(WIN32)
    set(CMAKE_INSTALL_PREFIX "C:/falcon" CACHE PATH "Installation prefix" FORCE)
  else()
    set(CMAKE_INSTALL_PREFIX "/opt/falcon" CACHE PATH "Installation prefix" FORCE)
  endif()
endif()
# ============================================================================
# CPack General Settings
# ============================================================================
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_SOURCE_IGNORE_FILES
  build/
  ".*~$"
  vcpkg/
  vcpkg_installed/
)
set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${CMAKE_PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

# ============================================================================
# Platform-Specific CPack Configuration
# ============================================================================
if(WIN32)
  set(CPACK_GENERATOR "ZIP")
  set(CPACK_ARCHIVE_THREADS 0)
else()
  # Linux: TGZ format (extractable via curl | tar xz)
  set(CPACK_GENERATOR "TGZ")
  set(CPACK_ARCHIVE_THREADS 0)
endif()

# ============================================================================
# Installation Directory Structure
# ============================================================================
set(INSTALL_BIN_DIR "bin")
set(INSTALL_LIB_DIR "lib")
set(INSTALL_INCLUDE_DIR "include")
set(INSTALL_SHARE_DIR "share")
set(INSTALL_DOC_DIR "doc")

# ============================================================================
# Install Targets
# ============================================================================

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/CMakeFiles/DependencyInfo.txt.in"
  "${CMAKE_BINARY_DIR}/FALCON_DEPENDENCIES.txt"
  @ONLY
)

install(
  FILES "${CMAKE_BINARY_DIR}/FALCON_DEPENDENCIES.txt"
  DESTINATION "."
)

install(
  FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
  DESTINATION "${INSTALL_DOC_DIR}"
)

# ============================================================================
# Install vcpkg dependencies
# ============================================================================
if(WIN32)
  file(GLOB_RECURSE WIN_DLLS "${VCPKG_INSTALLED_DIR}/x64-windows/bin/*.dll")
  install(
    PROGRAMS ${WIN_DLLS}
    DESTINATION "${INSTALL_BIN_DIR}"
  )
  file(GLOB_RECURSE WIN_LIBS "${VCPKG_INSTALLED_DIR}/x64-windows/lib/*.lib")
  install(
    FILES ${WIN_LIBS}
    DESTINATION "${INSTALL_LIB_DIR}"
  )
  file(GLOB_RECURSE WIN_INCLUDES "${VCPKG_INSTALLED_DIR}/x64-windows/include/*")
  install(
    FILES ${WIN_INCLUDES}
    DESTINATION "${INSTALL_INCLUDE_DIR}"
  )
  file(GLOB_RECURSE WIN_SHARE "${VCPKG_INSTALLED_DIR}/x64-windows/share/*")
  install(
    FILES ${WIN_SHARE}
    DESTINATION "${INSTALL_SHARE_DIR}"
  )
else()
  file(GLOB_RECURSE LINUX_BINS "${VCPKG_INSTALLED_DIR}/x64-linux-dynamic/bin/*")
  install(
    PROGRAMS ${LINUX_BINS}
    DESTINATION "${INSTALL_BIN_DIR}"
  )
  file(GLOB_RECURSE LINUX_LIBS_A "${VCPKG_INSTALLED_DIR}/x64-linux-dynamic/lib/*.a")
  file(GLOB_RECURSE LINUX_LIBS_SO "${VCPKG_INSTALLED_DIR}/x64-linux-dynamic/lib/*.so*")
  install(
    FILES ${LINUX_LIBS_A} ${LINUX_LIBS_SO}
    DESTINATION "${INSTALL_LIB_DIR}"
  )
  file(GLOB_RECURSE LINUX_INCLUDES "${VCPKG_INSTALLED_DIR}/x64-linux-dynamic/include/*")
  install(
    FILES ${LINUX_INCLUDES}
    DESTINATION "${INSTALL_INCLUDE_DIR}"
  )
  file(GLOB_RECURSE LINUX_SHARE "${VCPKG_INSTALLED_DIR}/x64-linux-dynamic/share/*")
  install(
    FILES ${LINUX_SHARE}
    DESTINATION "${INSTALL_SHARE_DIR}"
  )
endif()

# ============================================================================
# Install instrument-hub
# ============================================================================
set(GO_HUB_DIR "${CMAKE_BINARY_DIR}/instrument-hub")
set(GO_HUB_REPO "https://github.com/falcon-autotuning/falcon-instrument-hub.git")

# Clone if not already present
if(NOT EXISTS "${GO_HUB_DIR}/.git")
  execute_process(
    COMMAND git clone ${GO_HUB_REPO} ${GO_HUB_DIR}
    RESULT_VARIABLE GIT_CLONE_RESULT
  )
  if(NOT GIT_CLONE_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to clone falcon-instrument-hub repository")
  endif()
endif()

# ============================================================================
# Build the Go binary - FOR GIT BASH (with proper quoting)
# ============================================================================

set(GO_HUB_DIR "${CMAKE_BINARY_DIR}/instrument-hub")
set(GO_HUB_REPO "https://github.com/falcon-autotuning/falcon-instrument-hub.git")

# Clone if not already present
if(NOT EXISTS "${GO_HUB_DIR}/.git")
  execute_process(
    COMMAND git clone ${GO_HUB_REPO} ${GO_HUB_DIR}
    RESULT_VARIABLE GIT_CLONE_RESULT
  )
  if(NOT GIT_CLONE_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to clone falcon-instrument-hub repository")
  endif()
endif()

# Build the Go binary
if(WIN32)
  message(STATUS "Building Go program on Windows with CGO (Git Bash environment)")

  # Find clang-cl for MSVC-compatible compilation
  find_program(CLANG_CL_BIN clang-cl HINTS "C:/Program Files/LLVM/bin")

  if(NOT CLANG_CL_BIN)
    message(FATAL_ERROR "clang-cl not found. Please install LLVM with clang-cl support")
  endif()

  message(STATUS "Found clang-cl: ${CLANG_CL_BIN}")

  # Convert to forward slashes for bash
  string(REPLACE "\\" "/" CLANG_CL_BASH "${CLANG_CL_BIN}")
  message(STATUS "clang-cl path for bash: ${CLANG_CL_BASH}")
  # TODO: Fix this build in the Makefile in the instrument-hub
  # Create a bash script - with proper quoting for the CC variable
  set(BUILD_SCRIPT "${GO_HUB_DIR}/build-windows.sh")
  file(WRITE "${BUILD_SCRIPT}"
    "#!/bin/bash\n"
    "set -x\n"
    "echo 'Setting CC environment variable with proper quoting'\n"
    # Use quotes around the path and also wrap the entire export in quotes
    "export CC='${CLANG_CL_BASH}'\n"
    "export CXX='${CLANG_CL_BASH}'\n"
    "echo \"CC is now: $CC\"\n"
    "echo \"CXX is now: $CXX\"\n"
    "cd '${GO_HUB_DIR}'\n"
    "echo 'Current directory:'\n"
    "pwd\n"
    "echo 'Running: make build-go with quoted CC/CXX'\n"
    # Pass CC and CXX to make directly on the command line with quotes
    "cd runtime && CC="$(CC)" CXX="$(CXX)" go build -o bin/instrument-server.exe cmd/main.go; cd runtime && CC="$(CC)" CXX="$(CXX)" go build -o bin/dataviewer.exe ./cmd/dataviewer/
\n"
  )

  message(STATUS "Created bash build script: ${BUILD_SCRIPT}")

  # Run via bash
  message(STATUS "=== Running build via bash ===")
  execute_process(
    COMMAND bash "${BUILD_SCRIPT}"
    RESULT_VARIABLE GO_BUILD_RESULT
    OUTPUT_VARIABLE GO_BUILD_OUT
    ERROR_VARIABLE GO_BUILD_ERR
  )

  message(STATUS "Build result: ${GO_BUILD_RESULT}")
  message(STATUS "Build stdout:\n${GO_BUILD_OUT}")
  message(STATUS "Build stderr:\n${GO_BUILD_ERR}")
else()
  execute_process(
    COMMAND make build-go
    WORKING_DIRECTORY ${GO_HUB_DIR}
    RESULT_VARIABLE GO_BUILD_RESULT
    OUTPUT_VARIABLE GO_BUILD_OUT
    ERROR_VARIABLE GO_BUILD_ERR
  )
endif()

if(NOT GO_BUILD_RESULT EQUAL 0)
  message(WARNING "instrument-hub build stdout:\n${GO_BUILD_OUT}")
  message(WARNING "instrument-hub build stderr:\n${GO_BUILD_ERR}")
  message(FATAL_ERROR "Failed to build instrument-hub Go program")
endif()

message(STATUS "Successfully built instrument-hub Go program")

include(CPack)
