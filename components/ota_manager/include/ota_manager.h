#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <stdint.h>

#include "esp_err.h"
#include "network_manager.h"

typedef enum {
    OTA_MODE_AUTO = 0,   /* Install immediately when a newer version is found. */
    OTA_MODE_MANUAL      /* Notify the application and wait for confirmation. */
} ota_mode_t;

typedef struct {
    const char *version;
    const char *description;
    const char *release_date;
    size_t size;
} ota_firmware_info_t;

typedef esp_err_t (*ota_before_update_cb_t)(void *user_ctx);
typedef void (*ota_update_available_cb_t)(
    const ota_firmware_info_t *info,
    void *user_ctx
);

typedef struct {
    const char *manifest_url;
    /* Optional: NULL or empty for public OTA endpoints. */
    const char *auth_token;
    uint32_t check_interval_sec;
    /* Defaults to OTA_MODE_AUTO when zero-initialized. */
    ota_mode_t mode;
    /* Optional: called once when a new version is found in MANUAL mode. */
    ota_update_available_cb_t update_available;
    /* Optional: called after download and SHA-256 verification, before Flash. */
    ota_before_update_cb_t before_update;
    /* Shared context passed to both callbacks. */
    void *user_ctx;
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

/*
 * Confirm the firmware currently pending in OTA_MODE_MANUAL.
 * Returns ESP_OK without action if no update is waiting for confirmation.
 */
esp_err_t ota_manager_confirm_update(void);

#endif /* OTA_MANAGER_H */
