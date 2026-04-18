vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO falcon-autotuning/falcon-routine
    REF v${VERSION}
    SHA512 5298e448229dd2894b2484140612d6879a4965afbadfb989e8d57d95da20b962cf2115d4e0e66aaa85bb1d9d4f15636c2ec30d19b018a7cdf2d90577fec878ef
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
