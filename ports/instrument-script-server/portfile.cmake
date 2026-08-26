vcpkg_from_github(
      OUT_SOURCE_PATH SOURCE_PATH
      REPO falcon-autotuning/instrument-script-server
      REF v${VERSION}
      SHA512 531bea2023519ba58000e354f38ceafd745e64b65d38837cf766aaf0b77f020af3b3d42be69bc823866ad9097639d40ae4ecf6bcc07b93790f27236bd64203a3
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
