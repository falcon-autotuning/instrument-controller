vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO falcon-autotuning/visa-plugin
    REF v${VERSION}
    SHA512 3992a9c2cd2b7db01f6ca717a45e61dcaf3ffa28ab681e13bb18540ba071f18c3a28b8951a8ac84feb2b578ef7473d5f85b29b999d233234cf7f1e4e6fd29140
)
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DCMAKE_BUILD_TYPE=Release
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup()
file(INSTALL "${SOURCE_PATH}/LICENSE"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
     RENAME copyright)
vcpkg_copy_pdbs()
