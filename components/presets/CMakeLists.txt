idf_component_register(
    SRCS "src/presets.c"
    INCLUDE_DIRS "include"
    REQUIRES config_store nvs_flash log
)
