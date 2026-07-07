get_filename_component(WORKSPACE_ROOT "${CURRENT_PORT_DIR}/../../.." ABSOLUTE)
set(LOCAL_ISS_PATH "${WORKSPACE_ROOT}/instrument-script-server")
if(EXISTS "${LOCAL_ISS_PATH}")
    set(SOURCE_PATH "${LOCAL_ISS_PATH}")
else()
    vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO falcon-autotuning/instrument-script-server
        REF v${VERSION}
        SHA512 b87cad602a3187e654ee973f0918c02c11d61b038040ea0ce2fe0124a0fd8ccfdd6918e2fd6dec152e6115e1b92c342379366b6200397fca4229fa1f45189b0a
    )
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME instrument-script-server CONFIG_PATH share/instrument-script-server)



file(INSTALL "${SOURCE_PATH}/LICENSE"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
     RENAME copyright)

vcpkg_copy_pdbs()
