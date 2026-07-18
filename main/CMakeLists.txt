idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash led_driver color mapping effects pov wifi_mgr web_ui artnet sacn config_store status_led
)