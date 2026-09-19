vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO falcon-autotuning/instrument-script-server
    REF v${VERSION}
    SHA512 08de686fd32074e434d574874d5ae3484bcc664894298444e0b00a17af1392512bb527f2167da3f8266d3a29f967b81e1faddc9f1afe5e75d2e8b09feee62ca7
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_CLI=ON
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME instrument-script-server CONFIG_PATH share/instrument-script-server)

file(INSTALL "${SOURCE_PATH}/LICENSE"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
     RENAME copyright)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/share/doc/instrument-script-server/assets/icons")

vcpkg_copy_pdbs()
