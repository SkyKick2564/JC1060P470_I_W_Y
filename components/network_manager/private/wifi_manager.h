
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdint.h>
#include "esp_err.h"
#include "esp_wifi.h"

esp_err_t wifi_manager_init(void);

esp_err_t wifi_manager_set_config(
    const char *ssid,
    const char *password
);

esp_err_t wifi_manager_scan(void);

const wifi_ap_record_t *wifi_manager_get_scan_results(
    uint16_t *count,
    uint16_t *total_found
);

esp_err_t wifi_manager_start(void);

esp_err_t wifi_manager_connect(void);

esp_err_t wifi_manager_disconnect(void);

esp_err_t wifi_manager_stop(void);

esp_err_t wifi_manager_deinit(void);

#endif /* WIFI_MANAGER_H */
