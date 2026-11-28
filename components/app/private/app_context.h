#ifndef APP_CONTEXT_H
#define APP_CONTEXT_H

#include <stdbool.h>

#include "board_battery.h"
#include "network_manager.h"
#include "ota_manager.h"

#include "lvgl.h"
#include "esp_lcd_panel_ops.h"

#define APP_QUEUE_LENGTH       10
#define APP_TASK_STACK_SIZE    8192
#define APP_TASK_STACK_DEPTH  \
    (APP_TASK_STACK_SIZE / sizeof(StackType_t))

#define APP_TASK_PRIORITY      5

typedef enum {
    APP_REQUEST_STARTUP,

    APP_REQUEST_COMPONENT_READY,
    APP_REQUEST_COMPONENT_ERROR,

    APP_REQUEST_SHOW_SCREEN,

    APP_REQUEST_SHUTDOWN,
    APP_REQUEST_EXIT

} app_request_type_t;

typedef struct {
    app_request_type_t type;
    union {
        struct {
            esp_err_t error;
        } component_error;
        struct {
            uint8_t screen_id;
        } show_screen;
    } data;
} app_request_t;

typedef struct app_context{
    /* FreeRTOS Task */
    StaticTask_t task_buffer;
    StackType_t task_stack[APP_TASK_STACK_DEPTH];
    TaskHandle_t task;

    /* FreeRTOS Queue */
    StaticQueue_t queue_buffer;
    uint8_t queue_storage[
        APP_QUEUE_LENGTH * sizeof(app_request_t)
    ];
    QueueHandle_t queue;

    /* Display */
    lv_display_t *display;
    esp_lcd_panel_handle_t panel;

    /* Component Handles */
    network_manager_handle_t network;
    board_battery_handle_t battery;
    ota_manager_handle_t ota;
} app_context_t;

#endif /* APP_CONTEXT_H */