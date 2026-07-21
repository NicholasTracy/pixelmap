idf_component_register(
    SRCS "src/ota_update.c"
    INCLUDE_DIRS "include"
    REQUIRES app_update esp_partition log
)
