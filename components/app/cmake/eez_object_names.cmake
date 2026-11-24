set(_eez_screens_file "${CMAKE_CURRENT_LIST_DIR}/../src/ui/screens.c")

if(EXISTS "${_eez_screens_file}")
    file(READ "${_eez_screens_file}" _eez_screens_content)

    if(NOT _eez_screens_content MATCHES "LVGL_EEZ_NAMES_INJECTED")
        string(REGEX REPLACE
            "objects\\.([A-Za-z_][A-Za-z0-9_]*) = obj;"
            "objects.\\1 = obj;\n                    lv_obj_set_name(obj, \"\\1\");"
            _eez_screens_content
            "${_eez_screens_content}"
        )
        string(PREPEND _eez_screens_content "/* LVGL_EEZ_NAMES_INJECTED */\n")
        file(WRITE "${_eez_screens_file}" "${_eez_screens_content}")
    endif()
endif()
