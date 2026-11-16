#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t app_init(void);
void app_set_wifi_status(bool connected, bool rssi_valid, int8_t rssi);

#endif