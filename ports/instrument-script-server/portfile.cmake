vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO falcon-autotuning/instrument-script-server
    REF v${VERSION}
    SHA512 4b935148b8e0467142c8ae6b93a31035fa447021de0426113420642bc9678902de008e80aa34fb61c0e265ddbce29e6351a9c5c758afd890b42ab3eed3805db5
    PATCHES fix-grpc-case.patch
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_CLI=ON
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME instrument-script-server CONFIG_PATH share/instrument-script-server)
vcpkg_copy_tools(TOOL_NAMES instrument-script-server instrument-script-server-daemon instrument-worker AUTO_CLEAN)



file(INSTALL "${SOURCE_PATH}/LICENSE"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
     RENAME copyright)

vcpkg_copy_pdbs()
