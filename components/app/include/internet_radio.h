#ifndef INTERNET_RADIO_H
#define INTERNET_RADIO_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Subscribe Internet Radio to Network Manager connectivity events.
 *
 * Call after audio_manager_init() and network_manager_init(), but before
 * network_manager_start().
 */
esp_err_t internet_radio_init(void);

/**
 * @brief Unsubscribe Internet Radio from Network Manager events.
 */
esp_err_t internet_radio_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* INTERNET_RADIO_H */
