if("local-source" IN_LIST FEATURES)
    get_filename_component(WORKSPACE_ROOT "${CURRENT_PORT_DIR}/../../.." ABSOLUTE)
    set(LOCAL_TEAL_API_GEN_SOURCE "${WORKSPACE_ROOT}/teal-api-gen")

    if(NOT EXISTS "${LOCAL_TEAL_API_GEN_SOURCE}/CMakeLists.txt")
        message(FATAL_ERROR
            "Local teal-api-gen build requires a checkout at ${LOCAL_TEAL_API_GEN_SOURCE}"
        )
    endif()

    message(STATUS
        "teal-api-gen: using local source at ${LOCAL_TEAL_API_GEN_SOURCE}"
    )
    set(SOURCE_PATH "${LOCAL_TEAL_API_GEN_SOURCE}")
else()
    message(STATUS "teal-api-gen: using GitHub source v${VERSION}")
    vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO falcon-autotuning/teal-api-gen
        REF v${VERSION}
        SHA512 6e8571fb15485e8126e9c54741e355d66d29d172b04d2e45d3068c08b214631f37e8a1fcd595ffc5a6f7db216dc23601389a6228faaae1dde8990cf2017d5a32
    )
endif()
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
