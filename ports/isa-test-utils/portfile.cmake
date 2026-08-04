vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO falcon-autotuning/isa-test-utils
    REF v${VERSION}
    SHA512 61a2001af09ce8e927cff48da97815636196c551d1c97ea5cc6815c25573f743cd74666c368b93fed60d4a6afb2a30e09b191cfe4653d79f89565391e40b56de
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
