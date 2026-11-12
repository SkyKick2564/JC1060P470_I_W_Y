#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_wifi.h"

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
    NETWORK_EVENT_CONFIG_CHANGED,
    NETWORK_EVENT_SCAN_UPDATED
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
 * Saved Wi-Fi Profiles
 *
 * Up to 20 profiles are stored in NVS. Calling set_wifi_config() adds a new
 * SSID or updates the password of an existing SSID. If the list is full,
 * the last profile is replaced. When the manager is started, set_wifi_config()
 * saves the profile and asynchronously switches the active connection to it.
 * A successful return means the switch was accepted, not that Wi-Fi is connected;
 * monitor the normal Wi-Fi status events for the connection result.
 * clear_wifi_config() removes all profiles and is only allowed while stopped.
 */
#define NETWORK_MANAGER_MAX_SAVED_WIFI 20
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

/*
 * Wi-Fi AP scanning
 *
 * Start periodic scanning while the Wi-Fi screen is visible. The manager
 * refreshes its RAM snapshot and posts NETWORK_EVENT_SCAN_UPDATED.
 * Stop scanning when the screen is closed. The latest snapshot remains
 * readable until the next scan or manager deinitialization.
 */
#define NETWORK_MANAGER_MAX_SCAN_RESULTS 20

esp_err_t network_manager_start_scan(network_manager_handle_t handle);
esp_err_t network_manager_stop_scan(network_manager_handle_t handle);

/*
 * Copy the latest scan snapshot.
 * *count receives the number of records cached (up to capacity).
 * *total_found receives the total number of APs reported by the scan.
 * If capacity is too small, returns ESP_ERR_INVALID_SIZE and sets *count
 * to the required record count without copying records.
 */
esp_err_t network_manager_get_scan_results(
    network_manager_handle_t handle,
    wifi_ap_record_t *records,
    uint16_t capacity,
    uint16_t *count,
    uint16_t *total_found
);

#endif /* NETWORK_MANAGER_H */
