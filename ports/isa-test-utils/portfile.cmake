vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO falcon-autotuning/isa-test-utils
    REF v${VERSION}
    SHA512 c9815e0706b3352911ef76df10e6aaccf13eb0e7fe81a80d3c8772aeb453347687196486a39462982492023d51685f6d502ef2f2d6f5ae9ee636b0e9de1896c7
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
