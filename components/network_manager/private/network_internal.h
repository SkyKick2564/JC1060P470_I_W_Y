#ifndef NETWORK_INTERNAL_H
#define NETWORK_INTERNAL_H

#include "network_manager.h"

#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define NETWORK_MANAGER_QUEUE_LENGTH       16
#define NETWORK_MANAGER_TASK_STACK_SIZE    6144
#define NETWORK_MANAGER_TASK_PRIORITY      5
#define NETWORK_MANAGER_NVS_NAMESPACE      "network_mgr"
#define NETWORK_MANAGER_NVS_WIFI_KEY      "wifi_config"

typedef enum {
    NETWORK_MESSAGE_REQUEST,
    NETWORK_MESSAGE_WIFI_CONNECTED,
    NETWORK_MESSAGE_WIFI_DISCONNECTED,
    NETWORK_MESSAGE_GOT_IP
} network_message_type_t;

typedef enum {
    NETWORK_REQUEST_INIT,
    NETWORK_REQUEST_START,
    NETWORK_REQUEST_STOP,
    NETWORK_REQUEST_SET_CONFIG,
    NETWORK_REQUEST_CLEAR_CONFIG,
    NETWORK_REQUEST_DEINIT,
    NETWORK_REQUEST_EXIT
} network_request_type_t;

typedef struct {
    char ssid[33];
    char password[65];
} network_saved_wifi_config_t;

typedef struct {
    network_request_type_t type;
    SemaphoreHandle_t completed;
    esp_err_t result;
    union {
        network_saved_wifi_config_t wifi_config;
    } data;
} network_request_t;

typedef struct {
    network_message_type_t type;
    network_request_t *request;
    union {
        struct {
            char ssid[33];
        } connected;
        struct {
            uint8_t reason;
        } disconnected;
        esp_netif_ip_info_t ip_info;
    } data;
} network_message_t;

/* Internal Network Manager */
typedef struct network_manager {
    esp_netif_t *sta_netif;
    QueueHandle_t queue;
    TaskHandle_t task;
    SemaphoreHandle_t status_mutex;

    nvs_handle_t nvs_handle;
    bool nvs_opened;
    bool wifi_initialized;
    bool netif_initialized;
    bool initialized;
    bool started;
    bool config_valid;

    char ssid[33];
    char password[65];
    network_status_t status;

    bool scan_pending;
    TickType_t scan_due;

    esp_event_handler_instance_t wifi_event_instance;
    esp_event_handler_instance_t ip_event_instance;
} network_manager_t;

#endif /* NETWORK_INTERNAL_H */
