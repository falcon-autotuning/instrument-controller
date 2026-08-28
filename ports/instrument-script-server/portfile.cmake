vcpkg_from_github(
      OUT_SOURCE_PATH SOURCE_PATH
      REPO falcon-autotuning/instrument-script-server
      REF v${VERSION}
      SHA512 d02fbd065251adb9b5968c40669d8433bfa8b83e6edf58118516aa5ebadd41960161f83fdeb4272331cb06c138265266bcc3ebae6be1b4abb0ca8a9df5cf3f3e
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
