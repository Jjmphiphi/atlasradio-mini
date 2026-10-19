idf_build_get_property(python PYTHON)
set(SETUP_HTML_GZ "${CMAKE_CURRENT_BINARY_DIR}/setup.html.gz")
execute_process(
    COMMAND "${python}" -c
        "import gzip, pathlib, sys; src=pathlib.Path(sys.argv[1]); dst=pathlib.Path(sys.argv[2]); dst.write_bytes(gzip.compress(src.read_bytes(), 9))"
        "${CMAKE_CURRENT_LIST_DIR}/setup.html"
        "${SETUP_HTML_GZ}"
    RESULT_VARIABLE _setup_gz_rv
)
if(_setup_gz_rv)
    message(FATAL_ERROR "Failed to gzip setup.html for embedding")
endif()

idf_component_register(
    SRCS "http_server.c"
    INCLUDE_DIRS "."
    EMBED_FILES "${SETUP_HTML_GZ}"
    REQUIRES
        esp_http_server
        esp_http_client
        esp-tls
        app_update
        esp_partition
        audio
        services
        ws
        settings_ex
        json
        network
        ui
        playlist
        events
        espressif__zlib
        mqtt_svc
        espnow_link         # /api/espnow — pairing window for the hardware pilot
        sdcard
        spiffs
        fatfs
        board_pins
        secrets
        updater             # updater_www_stale() — shared web-UI staleness check
        diag                # /api/diag — snapshot shared with the Diagnostics screen
        net_wallpaper       # /api/wallpaper endpoints
        weather
        trace               # /api/settings "trace" — runtime diagnostic-log flags
)
