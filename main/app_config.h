#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "ota_manager.h"

/*
 * Application-level OTA settings.
 * Change these values here; ota_manager component remains reusable.
 */
static const ota_manager_config_t APP_OTA_CONFIG = {
    .manifest_url = "http://192.168.0.10:8080/manifest.json",
    .auth_token = "JC1060P470_OTA_2026_SecureKey",
    .check_interval_sec = 60,
};

#endif /* APP_CONFIG_H */
