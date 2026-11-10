#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include "esp_err.h"
#include "network_manager.h"

/*
 * Initialize OTA Manager with the existing Network Manager handle.
 * OTA Manager does not initialize, configure, or control Wi-Fi.
 */
esp_err_t ota_manager_init(network_manager_handle_t network);

/* Start the OTA polling FreeRTOS task. Safe to call once. */
esp_err_t ota_manager_start(void);

#endif /* OTA_MANAGER_H */
