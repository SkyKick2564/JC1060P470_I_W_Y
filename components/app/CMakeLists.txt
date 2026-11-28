
# Generate the EEZ Object Identifier -> objects_t pointer map from screens.h.
# This keeps the mapping synchronized when Objects are added or renamed in EEZ.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/src/ui/screens.h"
)
set(EEZ_OBJECT_MAP_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(EEZ_OBJECT_MAP_HEADER "${EEZ_OBJECT_MAP_DIR}/eez_object_map.h")
file(MAKE_DIRECTORY "${EEZ_OBJECT_MAP_DIR}")
file(WRITE "${EEZ_OBJECT_MAP_HEADER}"
    "#ifndef EEZ_OBJECT_MAP_H\n#define EEZ_OBJECT_MAP_H\n"
    "static const struct { lv_obj_t **object; const char *identifier; } s_eez_object_map[] = {\n"
)
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/src/ui/screens.h"
    EEZ_OBJECT_FIELDS
    REGEX "^[ \t]*lv_obj_t[ \t]*\\*[ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]*;"
)
foreach(EEZ_OBJECT_FIELD IN LISTS EEZ_OBJECT_FIELDS)
    string(REGEX REPLACE
        ".*\\*[ \t]*([A-Za-z_][A-Za-z0-9_]*)[ \t]*;.*"
        "\\1"
        EEZ_OBJECT_NAME
        "${EEZ_OBJECT_FIELD}"
    )
    string(LENGTH "${EEZ_OBJECT_NAME}" EEZ_OBJECT_NAME_LENGTH)
    if(EEZ_OBJECT_NAME_LENGTH GREATER 63)
        message(FATAL_ERROR
            "EEZ Object Identifier '${EEZ_OBJECT_NAME}' exceeds HOME_LAYOUT_ID_LENGTH (63 characters)"
        )
    endif()
    file(APPEND "${EEZ_OBJECT_MAP_HEADER}"
        "    { &objects.${EEZ_OBJECT_NAME}, \"${EEZ_OBJECT_NAME}\" },\n"
    )
endforeach()
file(APPEND "${EEZ_OBJECT_MAP_HEADER}" "};\n#endif\n")

idf_component_register(
    SRCS
        "src/app.c"
        "src/app_touch_multi.c"
        "src/actions.c"
        "src/home_actions.c"
        "src/control_center_actions.c"
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
        "src/ui/fonts/ui_font_detroit_.c"
        "src/ui/images/ui_image_lightning.c"
        "src/ui/images/ui_image_setting_icon.c"
        "src/ui/images/ui_image_camera_icon.c"
        "src/ui/images/ui_image_media_icon.c"
        "src/ui/images/ui_image_spotify_icon.c"
        "src/ui/images/ui_image_critical_icon.c"
        "src/ui/images/ui_image_display_icon.c"
    INCLUDE_DIRS
        "include"
        "src/ui"
        "${EEZ_OBJECT_MAP_DIR}"

    PRIV_INCLUDE_DIRS
        "private"

    REQUIRES
        lvgl
        esp_lvgl_port
        esp_board_manager
        esp_lcd_touch
        nvs_flash
        network_manager
        ota_manager
        board_battery
        freertos
        log
)