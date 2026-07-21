file(READ "${CMAKE_SOURCE_DIR}/VERSION" PIXELMAP_VERSION_RAW)
string(STRIP "${PIXELMAP_VERSION_RAW}" PIXELMAP_VERSION_STR)

idf_component_register(
    SRCS "src/web_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server json config_store mapping led_driver color effect_lua
    EMBED_TXTFILES
        "index.html"
        "vendor/bootstrap.min.css"
        "vendor/bootstrap.bundle.min.js"
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE PIXELMAP_VERSION=\"${PIXELMAP_VERSION_STR}\")
