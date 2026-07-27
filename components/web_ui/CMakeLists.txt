file(READ "${CMAKE_CURRENT_LIST_DIR}/../../VERSION" PIXELMAP_VERSION_RAW)
string(STRIP "${PIXELMAP_VERSION_RAW}" PIXELMAP_VERSION_STR)

# Regenerate boards/catalog.json from boards/*.json when present.
set(BOARDS_DIR "${CMAKE_CURRENT_LIST_DIR}/../../boards")
find_program(PIXELMAP_PYTHON NAMES python3 python)
if(PIXELMAP_PYTHON)
    execute_process(
        COMMAND "${PIXELMAP_PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../../tools/gen_boards_catalog.py"
                -o "${BOARDS_DIR}/catalog.json"
        RESULT_VARIABLE _pm_boards_rc
        OUTPUT_QUIET ERROR_QUIET
    )
    if(NOT _pm_boards_rc EQUAL 0)
        message(WARNING "gen_boards_catalog.py failed; using existing boards/catalog.json if present")
    endif()
endif()
if(NOT EXISTS "${BOARDS_DIR}/catalog.json")
    message(FATAL_ERROR "Missing ${BOARDS_DIR}/catalog.json — run: python tools/gen_boards_catalog.py")
endif()

idf_component_register(
    SRCS "src/web_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server json config_store mapping led_driver color effects effect_lua ota_update presets map_store media_store freertos audio wifi_mgr security esp_timer
    EMBED_TXTFILES
        "index.html"
        "vendor/bootstrap.min.css"
        "vendor/bootstrap.bundle.min.js"
        "../../boards/catalog.json"
    EMBED_FILES
        "../../docs/PixelMapLogo.png"
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE PIXELMAP_VERSION=\"${PIXELMAP_VERSION_STR}\")
