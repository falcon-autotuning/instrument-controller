vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO falcon-autotuning/falcon-core
    REF v${VERSION}
    SHA512 e9e21ff8af3426b598d5b1002b80dd79347c67b7bd821c79d4a6da1361982a25d633515a8a61fb3c103f1136e031f7866f0160a6363433c8f96c9519fbab766d
)

set(BUILD_C_API OFF)
if("c-api" IN_LIST FEATURES)
  set(BUILD_C_API ON)
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DFALCON_CORE_BUILD_C_API=${BUILD_C_API}
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup()

file(INSTALL "${SOURCE_PATH}/LICENSE.txt"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
     RENAME copyright)

vcpkg_copy_pdbs()
