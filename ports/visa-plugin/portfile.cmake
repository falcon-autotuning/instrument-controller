vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO falcon-autotuning/visa-plugin
    REF v${VERSION}
    SHA512 5499c5717e2ff2d6f4d6501ea1acb77a1ff0477ad64678fc5998cb0a7aa4212b5240131d1d3208f4663cb3a1d6f3013e9286cbdd33be6a2ee76927b8b357ae48
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
