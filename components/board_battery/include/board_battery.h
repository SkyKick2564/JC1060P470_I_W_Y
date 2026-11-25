#ifndef BOARD_BATTERY_H
#define BOARD_BATTERY_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/* Board Battery Event Base */
ESP_EVENT_DECLARE_BASE(ฺBOARD_BATTERY_EVENT);

typedef struct board_battery_context *board_battery_handle_t;

esp_err_t board_battery_init(board_battery_handle_t *handle);

esp_err_t board_battery_get_voltage(
    board_battery_handle_t handle,
    uint32_t *voltage_mv);

esp_err_t board_battery_get_percentage(
    board_battery_handle_t handle,
    uint8_t *percentage);

esp_err_t board_battery_get_charging_state(
    board_battery_handle_t handle,
    bool *is_charging);

esp_err_t board_battery_deinit(board_battery_handle_t handle);

#endif /* BOARD_BATTERY_H */
