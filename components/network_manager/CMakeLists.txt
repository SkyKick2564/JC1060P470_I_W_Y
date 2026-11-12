idf_component_register(
    SRCS
        "src/network_manager.c"
        "src/network_interface.c"
        "src/wifi_manager.c"

    INCLUDE_DIRS
        "include"

    PRIV_INCLUDE_DIRS
        "private"

    REQUIRES
        esp_event
        esp_common
        esp_wifi

    PRIV_REQUIRES
        esp_hosted
        esp_netif
        esp_wifi
        freertos
        nvs_flash
)
