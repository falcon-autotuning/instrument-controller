vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO falcon-autotuning/instrument-plugin-api
    REF v${VERSION}
    SHA512 331a3c4fb3ebf0531c38d60a76b31c076c71c7965dc2026a65ba673b6818b0ad3fad4826cf5a082d668f08491c935cd3bb4f0afff9f804d6ead9cb3d56407452
)

if("plugin" IN_LIST FEATURES)
  set(INSTRUMENT_PLUGIN_ENABLE_PLUGIN ON)
endif()

if("host" IN_LIST FEATURES)
  set(INSTRUMENT_PLUGIN_ENABLE_HOST ON)
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DINSTRUMENT_PLUGIN_ENABLE_PLUGIN=${INSTRUMENT_PLUGIN_ENABLE_PLUGIN}
        -DINSTRUMENT_PLUGIN_ENABLE_HOST=${INSTRUMENT_PLUGIN_ENABLE_HOST}
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup()

set(_config_file "${CURRENT_PACKAGES_DIR}/share/${PORT}/${PORT}-config.cmake")
file(APPEND "${_config_file}" [=[

# Compatibility aliases for consumers written against earlier target names.
if(TARGET instrument-plugin-api::instrument-plugin-api-plugin AND NOT TARGET instrument-plugin-api::plugin)
  add_library(instrument-plugin-api::plugin ALIAS instrument-plugin-api::instrument-plugin-api-plugin)
endif()

if(TARGET instrument-plugin-api::instrument-plugin-api-plugin AND NOT TARGET instrument-plugin-api::instrument-plugin-api)
  add_library(instrument-plugin-api::instrument-plugin-api ALIAS instrument-plugin-api::instrument-plugin-api-plugin)
endif()

if(TARGET instrument-plugin-api::instrument-plugin-api-host AND NOT TARGET instrument-plugin-api::host)
  add_library(instrument-plugin-api::host ALIAS instrument-plugin-api::instrument-plugin-api-host)
endif()
]=])

file(INSTALL "${SOURCE_PATH}/LICENSE"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
     RENAME copyright)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

vcpkg_copy_pdbs()
