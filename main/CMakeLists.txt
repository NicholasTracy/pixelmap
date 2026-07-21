idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash spiffs led_driver color mapping effects effect_lua pov wifi_mgr web_ui artnet sacn config_store status_led
)