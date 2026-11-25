#ifndef BOARD_BATTERY_H
#define BOARD_BATTERY_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#include "esp_event.h"
/* Board Battery Event Base */
ESP_EVENT_DECLARE_BASE(BOARD_BATTERY_EVENT);

typedef enum {
    BOARD_BATTERY_EVENT_DATA_UPDATED,
    BOARD_BATTERY_EVENT_CHARGING_CHANGED,
} board_battery_event_id_t;

typedef struct {
    float voltage;
    float percentage;
    bool is_charging;
} board_battery_event_data_t;

typedef struct board_battery_context *board_battery_handle_t;

esp_err_t board_battery_init(board_battery_handle_t *handle);

esp_err_t board_battery_get_voltage(
    board_battery_handle_t handle,
    float *voltage);

esp_err_t board_battery_get_percentage(
    board_battery_handle_t handle,
    float *percentage);

esp_err_t board_battery_get_charging_state(
    board_battery_handle_t handle,
    bool *is_charging);

esp_err_t board_battery_deinit(board_battery_handle_t handle);

#endif /* BOARD_BATTERY_H */
