
#ifndef NETWORK_INTERFACE_H
#define NETWORK_INTERFACE_H

#include "esp_err.h"
#include "esp_netif.h"

esp_err_t network_interface_init_sta(esp_netif_t **sta_netif);

void network_interface_deinit_sta(esp_netif_t *sta_netif);

#endif /* NETWORK_INTERFACE_H */
