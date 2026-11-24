#ifndef BOARD_BATTERY_H
#define BOARD_BATTERY_H

#include <stdint.h>

#include "esp_err.h"

/**
 * @brief Initialize battery voltage measurement and capacity estimation.
 *
 * @return ESP_OK on success, otherwise an error code.
 */
esp_err_t board_battery_init(void);

/**
 * @brief Get the measured battery voltage in volts.
 *
 * @param voltage Pointer to receive the battery voltage.
 * @return ESP_OK on success, otherwise an error code.
 */
esp_err_t board_battery_get_voltage(float *voltage);

/**
 * @brief Get the estimated battery capacity in percent (0-100).
 *
 * @param percentage Pointer to receive the estimated capacity.
 * @return ESP_OK on success, otherwise an error code.
 */
esp_err_t board_battery_get_percentage(uint8_t *percentage);

/**
 * @brief Deinitialize battery measurement and release its resources.
 *
 * @return ESP_OK on success, otherwise an error code.
 */
esp_err_t board_battery_deinit(void);

#endif /* BOARD_BATTERY_H */
