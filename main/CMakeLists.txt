idf_component_register(
    SRCS
        "main.c"
    INCLUDE_DIRS
        "."

    REQUIRES
        app
        network_manager
        ota_manager
        media_manager
        esp_board_manager
)
