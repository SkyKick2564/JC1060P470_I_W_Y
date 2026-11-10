# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(APP_VERSION_FILE "${CMAKE_CURRENT_LIST_DIR}/version.txt")
file(STRINGS "${APP_VERSION_FILE}" APP_VERSION LIMIT_COUNT 1)
if(NOT APP_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+(\\.[0-9]+)?$")
    message(FATAL_ERROR "version.txt must contain a numeric version such as 1.0.0 or 1.0.0.1")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(JC1060P470_I_W_Y VERSION ${APP_VERSION})
