vcpkg_from_github(
      OUT_SOURCE_PATH SOURCE_PATH
      REPO falcon-autotuning/instrument-script-server
      REF v${VERSION}
      SHA512 aba461c32d0e3fe9864ad9406faf8bbfbc572ffc2b365d89e650b00541705119509a09dc7a0c7e11bd715ff3786f0365b9f993e7e5b80bc6b62d872d672eddda
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
