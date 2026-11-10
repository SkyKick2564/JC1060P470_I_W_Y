#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include "esp_err.h"

/*
 * Start the local HTTP OTA service.
 *
 * Call after the network interface has obtained an IP address.
 * Safe to call more than once; subsequent calls are ignored.
 */
esp_err_t ota_manager_start(void);

#endif /* OTA_MANAGER_H */
