# ======================================================================
#               dmimg_traw - a decoder plugin for the tests
# ======================================================================
# "TRAW" images: magic, uint16_t width, height, then 0xAARRGGBB pixels.
# Decodes at 1/1 and 1/2 (every second pixel), in blocks of two rows.
set(DMOD_MODULE_NAME        dmimg_traw)
set(DMOD_MODULE_VERSION     ${PROJECT_VERSION})
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         1024)
set(DMOD_PRIORITY           0)
set(DMOD_DIF_IMPLS          dmimg)
unset(DMOD_DMR_PATH)            # a test module, not a release package

dmod_add_library(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    traw.c
)

target_link_libraries(${DMOD_MODULE_NAME} dmimg_if)
