#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "ota_manager.h"

/*
 * Public GitHub-hosted OTA manifest.
 * The manifest is maintained in the repository; firmware binaries are
 * attached to versioned GitHub Releases.
 */
static const ota_manager_config_t APP_OTA_CONFIG = {
    .manifest_url = "https://raw.githubusercontent.com/SkyKick2564/JC1060P470_I_W_Y/network-manager-freertos-task/ota/manifest.json",
    .auth_token = NULL,
    .check_interval_sec = 15,
};

// static const ota_manager_config_t APP_OTA_CONFIG = {
//     .manifest_url = "http://192.168.0.106:8080/manifest.json",
//     .auth_token = "MyCustomOTAKey2026",
//     .check_interval_sec = 15,
// };

#endif /* APP_CONFIG_H */
