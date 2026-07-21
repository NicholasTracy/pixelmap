file(READ "${CMAKE_SOURCE_DIR}/VERSION" PIXELMAP_VERSION_RAW)
string(STRIP "${PIXELMAP_VERSION_RAW}" PIXELMAP_VERSION_STR)

idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash spiffs led_driver color mapping effects effect_lua pov wifi_mgr web_ui artnet sacn config_store status_led
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE PIXELMAP_VERSION=\"${PIXELMAP_VERSION_STR}\")