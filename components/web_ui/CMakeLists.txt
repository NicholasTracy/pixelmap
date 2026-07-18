idf_component_register(
    SRCS "src/web_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server json config_store mapping led_driver
    EMBED_TXTFILES "www/index.html"
)