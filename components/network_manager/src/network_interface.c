
#include "network_interface.h"

#include "esp_event.h"
#include "esp_wifi_default.h"

esp_err_t network_interface_init_sta(esp_netif_t **sta_netif)
{
    if (sta_netif == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (*sta_netif != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Initialize TCP/IP stack */
    esp_err_t err = esp_netif_init();

    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    /* Create default event loop if it does not exist */
    err = esp_event_loop_create_default();

    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    /* Create Wi-Fi Station Network Interface */
    *sta_netif = esp_netif_create_default_wifi_sta();

    if (*sta_netif == NULL) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

void network_interface_deinit_sta(esp_netif_t *sta_netif)
{
    if (sta_netif != NULL) {
        esp_netif_destroy_default_wifi(sta_netif);
    }
}
