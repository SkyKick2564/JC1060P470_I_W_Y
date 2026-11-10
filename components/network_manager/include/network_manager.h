#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_event.h"

/* Network Manager Event Base */
ESP_EVENT_DECLARE_BASE(NETWORK_MANAGER_EVENT);

typedef enum {
    NETWORK_STATE_UNINITIALIZED,
    NETWORK_STATE_INITIALIZED,
    NETWORK_STATE_STARTED,
    NETWORK_STATE_DISCONNECTED,
    NETWORK_STATE_WIFI_CONNECTED,
    NETWORK_STATE_IP_READY,
    NETWORK_STATE_STOPPED
} network_state_t;

typedef enum {
    NETWORK_EVENT_STARTED,
    NETWORK_EVENT_STOPPED,
    NETWORK_EVENT_WIFI_CONNECTED,
    NETWORK_EVENT_WIFI_DISCONNECTED,
    NETWORK_EVENT_GOT_IP,
    NETWORK_EVENT_LOST_IP,
    NETWORK_EVENT_INTERNET_CONNECTED,
    NETWORK_EVENT_INTERNET_DISCONNECTED,
    NETWORK_EVENT_CONFIG_CHANGED
} network_event_id_t;

/* Current Network Status */
typedef struct {
    network_state_t state;

    bool wifi_connected;
    bool has_ip;

    char ssid[33];
    char ip_address[16];
    char gateway[16];
} network_status_t;

/* Network Manager Handle */
typedef struct network_manager *network_manager_handle_t;

/*
 * Lifecycle
 *
 * The manager owns a FreeRTOS task and command/event queue.
 * Public control APIs submit commands to that task and wait for completion.
 */
esp_err_t network_manager_init(network_manager_handle_t *out_handle);
esp_err_t network_manager_start(network_manager_handle_t handle);
esp_err_t network_manager_stop(network_manager_handle_t handle);
esp_err_t network_manager_deinit(network_manager_handle_t handle);

/*
 * Wi-Fi Configuration
 *
 * Configuration is stored in NVS and survives a reboot.
 * Set/clear configuration while stopped; start the manager afterward.
 */
esp_err_t network_manager_set_wifi_config(
    network_manager_handle_t handle,
    const char *ssid,
    const char *password
);

esp_err_t network_manager_clear_wifi_config(
    network_manager_handle_t handle
);

/* Network Status */
esp_err_t network_manager_get_status(
    network_manager_handle_t handle,
    network_status_t *status
);

#endif /* NETWORK_MANAGER_H */
