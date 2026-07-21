idf_component_register(
    SRCS "src/effect_lua.c"
    INCLUDE_DIRS "include"
    REQUIRES lua color nvs_flash freertos
)

# lua_register() uses a comma-expression; ESP-IDF treats -Wunused-value as error
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-value)
