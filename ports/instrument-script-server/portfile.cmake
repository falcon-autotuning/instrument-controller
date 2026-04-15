vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO falcon-autotuning/instrument-script-server
    REF v${VERSION}
    SHA512 33b91e1cf0a2d1610ee404b8d00a046d894cc0f22d012e5eae51f3c635490d2cddea62575523192a7af4b248a5f1b801f53f81d0c2892200eeaecad7637a80c9
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup()

file(INSTALL "${SOURCE_PATH}/LICENSE"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
     RENAME copyright)

vcpkg_copy_pdbs()
