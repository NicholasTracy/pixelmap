cmake_minimum_required(VERSION 3.16)
project(pixelmap_virtbench C)

set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

find_package(PkgConfig REQUIRED)
pkg_check_modules(CJSON REQUIRED libcjson)

add_executable(virtbench
  main.c
  stubs/effect_lua_stub.c
  "${ROOT}/components/color/src/color_engine.c"
  "${ROOT}/components/mapping/src/pixel_map.c"
  "${ROOT}/components/pov/src/pov.c"
  "${ROOT}/components/effects/src/effects.c"
  "${ROOT}/components/led_driver/src/led_encode.c"
)

target_include_directories(virtbench PRIVATE
  stubs
  "${ROOT}/components/color/include"
  "${ROOT}/components/mapping/include"
  "${ROOT}/components/pov/include"
  "${ROOT}/components/effects/include"
  "${ROOT}/components/effect_lua/include"
  "${ROOT}/components/led_driver/include"
  ${CJSON_INCLUDE_DIRS}
)

# ESP-IDF json component exposes "cJSON.h"; Debian/Ubuntu uses cjson/cJSON.h
target_compile_options(virtbench PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_compile_definitions(virtbench PRIVATE PM_HOST_VIRTBENCH=1)
target_link_libraries(virtbench PRIVATE m ${CJSON_LIBRARIES})
target_link_directories(virtbench PRIVATE ${CJSON_LIBRARY_DIRS})

# Allow #include "cJSON.h" when headers live in .../cjson/
if (EXISTS "/usr/include/cjson/cJSON.h")
  target_include_directories(virtbench PRIVATE /usr/include/cjson)
endif()
