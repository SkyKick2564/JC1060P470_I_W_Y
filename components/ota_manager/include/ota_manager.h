#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <stdint.h>

#include "esp_err.h"
#include "network_manager.h"

typedef struct {
    const char *manifest_url;
    /* Optional: NULL or empty for public OTA endpoints. */
    const char *auth_token;
    uint32_t check_interval_sec;
} ota_manager_config_t;

/*
 * Initialize OTA Manager with the existing Network Manager handle and
 * application-provided configuration. OTA Manager does not control Wi-Fi.
 */
esp_err_t ota_manager_init(
    network_manager_handle_t network,
    const ota_manager_config_t *config
);

/* Start the OTA polling FreeRTOS task. Safe to call once. */
esp_err_t ota_manager_start(void);

#endif /* OTA_MANAGER_H */
