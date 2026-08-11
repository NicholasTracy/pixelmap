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

# Gzip large static UI assets so the firmware fits dual-OTA slots on boards that
# still run stock WLED partitions (no USB repartition / partition-table change).
if(NOT PIXELMAP_PYTHON)
    message(FATAL_ERROR "python3/python required to gzip embed web UI assets")
endif()
set(PM_GZ_DIR "${CMAKE_CURRENT_BINARY_DIR}/gzipped")
file(MAKE_DIRECTORY "${PM_GZ_DIR}")
set(PM_GZ_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/../../tools/gzip_asset.py")
set(PM_GZ_INDEX "${PM_GZ_DIR}/index.html.gz")
set(PM_GZ_CSS "${PM_GZ_DIR}/bootstrap.min.css.gz")
set(PM_GZ_JS "${PM_GZ_DIR}/bootstrap.bundle.min.js.gz")
set(PM_SRC_INDEX "${CMAKE_CURRENT_LIST_DIR}/index.html")
set(PM_SRC_CSS "${CMAKE_CURRENT_LIST_DIR}/vendor/bootstrap.min.css")
set(PM_SRC_JS "${CMAKE_CURRENT_LIST_DIR}/vendor/bootstrap.bundle.min.js")

execute_process(
    COMMAND "${PIXELMAP_PYTHON}" "${PM_GZ_SCRIPT}" "${PM_SRC_INDEX}" "${PM_GZ_INDEX}"
    RESULT_VARIABLE _pm_gz_rc
    OUTPUT_VARIABLE _pm_gz_out
    ERROR_VARIABLE _pm_gz_err
)
if(NOT _pm_gz_rc EQUAL 0)
    message(FATAL_ERROR "gzip index.html failed: ${_pm_gz_err}")
endif()
message(STATUS "${_pm_gz_out}")
execute_process(
    COMMAND "${PIXELMAP_PYTHON}" "${PM_GZ_SCRIPT}" "${PM_SRC_CSS}" "${PM_GZ_CSS}"
    RESULT_VARIABLE _pm_gz_rc
    OUTPUT_VARIABLE _pm_gz_out
    ERROR_VARIABLE _pm_gz_err
)
if(NOT _pm_gz_rc EQUAL 0)
    message(FATAL_ERROR "gzip bootstrap.min.css failed: ${_pm_gz_err}")
endif()
message(STATUS "${_pm_gz_out}")
execute_process(
    COMMAND "${PIXELMAP_PYTHON}" "${PM_GZ_SCRIPT}" "${PM_SRC_JS}" "${PM_GZ_JS}"
    RESULT_VARIABLE _pm_gz_rc
    OUTPUT_VARIABLE _pm_gz_out
    ERROR_VARIABLE _pm_gz_err
)
if(NOT _pm_gz_rc EQUAL 0)
    message(FATAL_ERROR "gzip bootstrap.bundle.min.js failed: ${_pm_gz_err}")
endif()
message(STATUS "${_pm_gz_out}")

# Re-run cmake (and regenerate gzip) when UI sources change.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PM_SRC_INDEX}" "${PM_SRC_CSS}" "${PM_SRC_JS}" "${PM_GZ_SCRIPT}")

idf_component_register(
    SRCS "src/web_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server json config_store mapping led_driver color effects effect_lua ota_update presets map_store media_store freertos audio wifi_mgr security esp_timer
    EMBED_TXTFILES
        "../../boards/catalog.json"
    EMBED_FILES
        "../../docs/PixelMapLogo.png"
        "${PM_GZ_INDEX}"
        "${PM_GZ_CSS}"
        "${PM_GZ_JS}"
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE PIXELMAP_VERSION=\"${PIXELMAP_VERSION_STR}\")
