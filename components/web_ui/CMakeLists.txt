idf_component_register(
    SRCS "src/web_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server json config_store mapping led_driver effect_lua
    EMBED_TXTFILES
        "index.html"
        "vendor/bootstrap.min.css"
        "vendor/bootstrap.bundle.min.js"
)
