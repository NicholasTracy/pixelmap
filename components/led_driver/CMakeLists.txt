idf_component_register(
    SRCS "src/led_chipsets.c" "src/led_strip.c"
    INCLUDE_DIRS "include"
    REQUIRES driver color
)