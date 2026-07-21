idf_component_register(
    SRCS "src/audio.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_timer freertos effects
)
