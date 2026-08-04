vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO falcon-autotuning/visa-plugin
    REF v${VERSION}
    SHA512 56d16556d889b2927c6a15dedf5605ce277c4cb3b1e0d4d94b5c6419f06012661f0aa81d6673ac4f220a03ed2b97029debebb5489995e5eac70a199480ccb004
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
