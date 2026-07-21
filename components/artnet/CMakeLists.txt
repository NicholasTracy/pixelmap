idf_component_register(
    SRCS "src/artnet.c"
    INCLUDE_DIRS "include"
    REQUIRES lwip esp_timer esp_netif
)
