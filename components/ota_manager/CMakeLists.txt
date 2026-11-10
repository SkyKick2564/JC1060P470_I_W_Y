idf_component_register(
    SRCS
        "src/ota_manager.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        esp_http_server
        app_update
        esp_event
        esp_system
        freertos
        esp_partition
)