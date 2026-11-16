idf_component_register(
    SRCS
        "src/ui/ui.c"
        "src/ui/screens.c"
        "src/ui/styles.c"
        "src/ui/images.c"
        "src/ui/fonts/ui_font_noto_sans_thai18.c"
        "src/ui/fonts/ui_font_noto_sans_thai_22.c"
        "src/ui/fonts/ui_font_noto_sans_thai_24.c"
        "src/ui/fonts/ui_font_noto_sans_thai_26.c"
        "src/ui/fonts/ui_font_noto_sans_thai_32.c"
        "src/ui/fonts/ui_font_noto_sans_thai_40.c"

    INCLUDE_DIRS
        "src/ui"

    REQUIRES
        lvgl
        esp_lvgl_port
)