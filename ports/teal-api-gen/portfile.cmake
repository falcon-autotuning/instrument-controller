
message(STATUS "teal-api-gen: using GitHub source v${VERSION}")
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO falcon-autotuning/teal-api-gen
    REF v${VERSION}
    SHA512 0189559a1155fcae0233f14297607992bcd41f4493a26893d5faf03e4191b9775114890d8f517dbddf3533ec953df1b72530d66c2fe68ba4544c91df163f3c35
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
