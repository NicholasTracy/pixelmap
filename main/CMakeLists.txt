# CMAKE_SOURCE_DIR may be the build dir during early IDF requirement scans.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../VERSION" PIXELMAP_VERSION_RAW)
string(STRIP "${PIXELMAP_VERSION_RAW}" PIXELMAP_VERSION_STR)

idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash led_driver color mapping map_store effects effect_lua pov wifi_mgr web_ui artnet sacn config_store status_led presets audio
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE PIXELMAP_VERSION=\"${PIXELMAP_VERSION_STR}\")