idf_component_register(
    SRCS
        "main.c"
    INCLUDE_DIRS
        "."

    REQUIRES
        network_manager
        esp_board_manager
)