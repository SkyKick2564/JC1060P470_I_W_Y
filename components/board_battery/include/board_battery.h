#ifndef BOARD_BATTERY_H
#define BOARD_BATTERY_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BOARD_BATTERY_STATE_UNKNOWN = 0,
    BOARD_BATTERY_STATE_CHARGING,
    BOARD_BATTERY_STATE_DISCHARGING,
    BOARD_BATTERY_STATE_FULL
} board_battery_state_t;

typedef struct {
    float voltage;                  // Battery voltage (V)
    uint8_t percentage;             // Battery level (%)
    board_battery_state_t state;    // Estimated battery state
} board_battery_info_t;


esp_err_t board_battery_init(void);

#endif /* BOARD_BATTERY_H */