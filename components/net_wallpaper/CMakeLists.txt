idf_component_register(
    SRCS
        "net_asset.c"
        "net_wallpaper_stub.c"
    INCLUDE_DIRS "."
    REQUIRES
        lvgl
        esp_http_client
        esp-tls
        json
        app_state
        services
        settings_ex
        sdcard
)
