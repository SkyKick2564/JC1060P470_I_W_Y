
#ifndef NETWORK_INTERNAL_H
#define NETWORK_INTERNAL_H

#include "network_manager.h"

#include "esp_netif.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/* Internal Wi-Fi/IP Event Message */
typedef struct {
    esp_event_base_t event_base;
    int32_t event_id;

    union {
        wifi_event_sta_connected_t sta_connected;
        wifi_event_sta_disconnected_t sta_disconnected;
        ip_event_got_ip_t got_ip;
    } data;

} network_event_message_t;

/* Internal Network Manager */
typedef struct network_manager {

    /* Network Interface */
    esp_netif_t *sta_netif;

    /* Manager RTOS */
    QueueHandle_t command_queue;
    TaskHandle_t manager_task;

    /* Wi-Fi Configuration */
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
