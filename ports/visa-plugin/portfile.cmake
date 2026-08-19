vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO falcon-autotuning/visa-plugin
    REF v${VERSION}
    SHA512 44dd1402882fccbbea1ba6dbd731185bba649c78000dd28d024ab090cae5bc623c3887f7f5ed053c55035c29c5fea78262b8fe7645cf783960f981996d023ee7
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
