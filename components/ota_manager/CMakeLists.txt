idf_component_register(
    SRCS
        "src/ota_manager.c"

    INCLUDE_DIRS
        "include"

    PRIV_INCLUDE_DIRS
        "private"
        
    REQUIRES
        network_manager
        esp_http_client
        app_update
        esp_partition
        esp_app_format

        mbedtls
        esp_system
        heap
        esp_timer
        freertos
        log
)
