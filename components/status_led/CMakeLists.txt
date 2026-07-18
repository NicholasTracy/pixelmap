idf_component_register(
    SRCS "src/status_led.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_timer
)