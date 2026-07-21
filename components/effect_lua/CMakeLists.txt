idf_component_register(
    SRCS "src/effect_lua.c"
    INCLUDE_DIRS "include"
    REQUIRES lua color nvs_flash freertos
)
