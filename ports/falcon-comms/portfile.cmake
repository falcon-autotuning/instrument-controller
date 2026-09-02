vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO falcon-autotuning/falcon-comms
    REF v${VERSION}
    SHA512 c55d4a9b60b9e3fe24ced871b191b3c783c5339456064176fc22ae9a218ac3cae024d4aafcacb8543b0f5b84d26d15d3a928e49c6f6426596d575ffe66a8caef
)

# Override natsManager.cpp with the locally-fixed version from the falcon-comms
# workspace clone until the fix is pushed to GitHub and a new version released.
# The fix replaces natsSubscription_NextMsg (invalid for pull consumers in cnats v3)
# with natsSubscription_Fetch.
get_filename_component(WORKSPACE_ROOT "${CURRENT_PORT_DIR}/../../.." ABSOLUTE)
set(FALCON_NATS_OVERRIDE "${WORKSPACE_ROOT}/falcon-comms/src/natsManager.cpp")
if(EXISTS "${FALCON_NATS_OVERRIDE}")
    file(COPY "${FALCON_NATS_OVERRIDE}" DESTINATION "${SOURCE_PATH}/src")
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup()
file(INSTALL "${SOURCE_PATH}/LICENSE"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
     RENAME copyright)
vcpkg_copy_pdbs()
