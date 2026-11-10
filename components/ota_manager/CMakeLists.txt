idf_component_register(
    SRCS
        "src/ota_manager.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        network_manager
        esp_http_client
        app_update
        esp_partition
        esp_app_format
        json
        mbedtls
        esp_system
        freertos
        log
)
