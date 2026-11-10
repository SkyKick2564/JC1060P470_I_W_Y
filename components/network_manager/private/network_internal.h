#ifndef NETWORK_INTERNAL_H
#define NETWORK_INTERNAL_H

#include "network_manager.h"

#include "esp_netif.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* Internal Network Manager */
typedef struct network_manager {
    /* Network Interface */
    esp_netif_t *sta_netif;

    /* Protects status and shared configuration */
    SemaphoreHandle_t mutex;

    /* Wi-Fi Configuration (RAM only in this implementation) */
    bool config_valid;
    char ssid[33];
    char password[65];

    /* Network State */
    network_status_t status;

    /* Lifecycle */
    bool initialized;
    bool started;

    /* Event Handler Instances */
    esp_event_handler_instance_t wifi_event_instance;
    esp_event_handler_instance_t ip_event_instance;
} network_manager_t;

#endif /* NETWORK_INTERNAL_H */
