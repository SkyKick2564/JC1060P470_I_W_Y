#include "network_internal.h"

#include <stdlib.h>
#include <string.h>

#include "network_interface.h"
#include "wifi_manager.h"

#include "eh_host_core.h"
#include "esp_log.h"
#include "nvs_flash.h"

#define NETWORK_MANAGER_HOSTED_TIMEOUT_MS 10000
#define NETWORK_MANAGER_RETRY_INITIAL_MS  1000
#define NETWORK_MANAGER_RETRY_MAX_MS     30000

static const char *TAG = "network_manager";

static void network_manager_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
);

ESP_EVENT_DEFINE_BASE(NETWORK_MANAGER_EVENT);

static void network_manager_post_event(network_event_id_t event_id)
{
    esp_err_t err = esp_event_post(
        NETWORK_MANAGER_EVENT,
        event_id,
        NULL,
        0,
        0
    );

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to post event %d: %s",
                 event_id, esp_err_to_name(err));
    }
}

static void network_manager_schedule_retry(network_manager_t *manager)
{
    if (!manager->started || !manager->config_valid) {
        manager->retry_pending = false;
        return;
    }

    uint32_t delay_ms = NETWORK_MANAGER_RETRY_INITIAL_MS;

    for (uint8_t i = 0; i < manager->retry_attempt; ++i) {
        if (delay_ms >= NETWORK_MANAGER_RETRY_MAX_MS / 2) {
            delay_ms = NETWORK_MANAGER_RETRY_MAX_MS;
            break;
        }
        delay_ms *= 2;
    }

    if (delay_ms > NETWORK_MANAGER_RETRY_MAX_MS) {
        delay_ms = NETWORK_MANAGER_RETRY_MAX_MS;
    }

    if (manager->retry_attempt < 6) {
        manager->retry_attempt++;
    }

    manager->retry_due = xTaskGetTickCount() + pdMS_TO_TICKS(delay_ms);
    manager->retry_pending = true;

    ESP_LOGW(TAG, "Reconnect scheduled in %lu ms",
             (unsigned long)delay_ms);
}

static esp_err_t network_manager_load_wifi_config(network_manager_t *manager)
{
    network_saved_wifi_config_t saved = {0};
    size_t size = sizeof(saved);

    esp_err_t err = nvs_get_blob(
        manager->nvs_handle,
        NETWORK_MANAGER_NVS_WIFI_KEY,
        &saved,
        &size
    );

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        manager->config_valid = false;
        return ESP_OK;
    }

    if (err != ESP_OK) {
        return err;
    }

    if (size != sizeof(saved) ||
        saved.ssid[0] == '\0' ||
        memchr(saved.ssid, '\0', sizeof(saved.ssid)) == NULL ||
        memchr(saved.password, '\0', sizeof(saved.password)) == NULL) {
        ESP_LOGE(TAG, "Stored Wi-Fi configuration is invalid");
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(manager->ssid, saved.ssid, sizeof(manager->ssid));
    memcpy(manager->password, saved.password, sizeof(manager->password));
    manager->config_valid = true;

    ESP_LOGI(TAG, "Saved Wi-Fi configuration loaded");
    return ESP_OK;
}

static esp_err_t network_manager_save_wifi_config(
    network_manager_t *manager,
    const network_saved_wifi_config_t *config)
{
    esp_err_t err = nvs_set_blob(
        manager->nvs_handle,
        NETWORK_MANAGER_NVS_WIFI_KEY,
        config,
        sizeof(*config)
    );

    if (err != ESP_OK) {
        return err;
    }

    return nvs_commit(manager->nvs_handle);
}

static esp_err_t network_manager_erase_wifi_config(network_manager_t *manager)
{
    esp_err_t err = nvs_erase_key(
        manager->nvs_handle,
        NETWORK_MANAGER_NVS_WIFI_KEY
    );

    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    return nvs_commit(manager->nvs_handle);
}

static esp_err_t network_manager_hw_deinit(network_manager_t *manager)
{
    if (manager->ip_event_instance != NULL) {
        esp_err_t err = esp_event_handler_instance_unregister(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            manager->ip_event_instance
        );
        if (err != ESP_OK) {
            return err;
        }
        manager->ip_event_instance = NULL;
    }

    if (manager->wifi_event_instance != NULL) {
        esp_err_t err = esp_event_handler_instance_unregister(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            manager->wifi_event_instance
        );
        if (err != ESP_OK) {
            return err;
        }
        manager->wifi_event_instance = NULL;
    }

    if (manager->wifi_initialized) {
        esp_err_t err = wifi_manager_deinit();
        if (err != ESP_OK) {
            return err;
        }
        manager->wifi_initialized = false;
    }

    if (manager->netif_initialized) {
        network_interface_deinit_sta(manager->sta_netif);
        manager->sta_netif = NULL;
        manager->netif_initialized = false;
    }

    if (manager->nvs_opened) {
        nvs_close(manager->nvs_handle);
        manager->nvs_opened = false;
    }

    return ESP_OK;
}

static esp_err_t network_manager_hw_init(network_manager_t *manager)
{
    int hosted_result = eh_host_wait_auto_init_ready(
        NETWORK_MANAGER_HOSTED_TIMEOUT_MS
    );

    if (hosted_result != 0) {
        ESP_LOGE(TAG, "ESP-Hosted initialization failed: %d", hosted_result);
        return ESP_FAIL;
    }

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "NVS initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_open(
        NETWORK_MANAGER_NVS_NAMESPACE,
        NVS_READWRITE,
        &manager->nvs_handle
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }
    manager->nvs_opened = true;

    err = network_manager_load_wifi_config(manager);
    if (err != ESP_OK) {
        return err;
    }

    err = network_interface_init_sta(&manager->sta_netif);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "STA interface initialization failed: %s",
                 esp_err_to_name(err));
        return err;
    }
    manager->netif_initialized = true;

    err = wifi_manager_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi initialization failed: %s", esp_err_to_name(err));
        return err;
    }
    manager->wifi_initialized = true;

    err = esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        network_manager_event_handler,
        manager,
        &manager->wifi_event_instance
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi event registration failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    err = esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        network_manager_event_handler,
        manager,
        &manager->ip_event_instance
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "IP event registration failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    manager->initialized = true;
    manager->status.state = NETWORK_STATE_INITIALIZED;

    ESP_LOGI(TAG, "Network Manager hardware initialized");
    return ESP_OK;
}

static void network_manager_process_wifi_connected(
    network_manager_t *manager,
    const char *ssid)
{
    xSemaphoreTake(manager->status_mutex, portMAX_DELAY);

    size_t ssid_len = strnlen(ssid, sizeof(manager->status.ssid) - 1);
    memcpy(manager->status.ssid, ssid, ssid_len);
    manager->status.ssid[ssid_len] = '\0';
    manager->status.wifi_connected = true;
    manager->status.has_ip = false;
    manager->status.ip_address[0] = '\0';
    manager->status.gateway[0] = '\0';
    manager->status.state = NETWORK_STATE_WIFI_CONNECTED;

    xSemaphoreGive(manager->status_mutex);

    ESP_LOGI(TAG, "Wi-Fi connected: %s", ssid);
    network_manager_post_event(NETWORK_EVENT_WIFI_CONNECTED);
}

static void network_manager_process_wifi_disconnected(
    network_manager_t *manager,
    uint8_t reason)
{
    bool had_ip;

    xSemaphoreTake(manager->status_mutex, portMAX_DELAY);

    had_ip = manager->status.has_ip;
    manager->status.wifi_connected = false;
    manager->status.has_ip = false;
    manager->status.ssid[0] = '\0';
    manager->status.ip_address[0] = '\0';
    manager->status.gateway[0] = '\0';
    manager->status.state = manager->started
        ? NETWORK_STATE_DISCONNECTED
        : NETWORK_STATE_STOPPED;

    xSemaphoreGive(manager->status_mutex);

    ESP_LOGW(TAG, "Wi-Fi disconnected, reason: %u", reason);

    if (had_ip) {
        network_manager_post_event(NETWORK_EVENT_LOST_IP);
    }
    network_manager_post_event(NETWORK_EVENT_WIFI_DISCONNECTED);

    if (manager->started && manager->config_valid) {
        network_manager_schedule_retry(manager);
    } else {
        manager->retry_pending = false;
    }
}

static void network_manager_process_got_ip(
    network_manager_t *manager,
    const esp_netif_ip_info_t *ip_info)
{
    xSemaphoreTake(manager->status_mutex, portMAX_DELAY);

    manager->status.wifi_connected = true;
    manager->status.has_ip = true;
    manager->status.state = NETWORK_STATE_IP_READY;

    esp_ip4addr_ntoa(
        &ip_info->ip,
        manager->status.ip_address,
        sizeof(manager->status.ip_address)
    );

    esp_ip4addr_ntoa(
        &ip_info->gw,
        manager->status.gateway,
        sizeof(manager->status.gateway)
    );

    ESP_LOGI(TAG, "Got IP: %s", manager->status.ip_address);
    ESP_LOGI(TAG, "Gateway: %s", manager->status.gateway);

    xSemaphoreGive(manager->status_mutex);

    manager->retry_pending = false;
    manager->retry_attempt = 0;
    network_manager_post_event(NETWORK_EVENT_GOT_IP);
}

static void network_manager_process_event(
    network_manager_t *manager,
    const network_message_t *message)
{
    switch (message->type) {
    case NETWORK_MESSAGE_WIFI_CONNECTED:
        network_manager_process_wifi_connected(
            manager,
            message->data.connected.ssid
        );
        break;

    case NETWORK_MESSAGE_WIFI_DISCONNECTED:
        network_manager_process_wifi_disconnected(
            manager,
            message->data.disconnected.reason
        );
        break;

    case NETWORK_MESSAGE_GOT_IP:
        network_manager_process_got_ip(
            manager,
            &message->data.ip_info
        );
        break;

    default:
        break;
    }
}

static esp_err_t network_manager_process_request(
    network_manager_t *manager,
    network_request_t *request,
    bool *exit_task)
{
    esp_err_t err = ESP_OK;
    *exit_task = false;

    switch (request->type) {
    case NETWORK_REQUEST_INIT:
        err = network_manager_hw_init(manager);
        if (err != ESP_OK) {
            esp_err_t cleanup_err = network_manager_hw_deinit(manager);
            if (cleanup_err != ESP_OK) {
                ESP_LOGE(TAG, "Initialization rollback failed: %s",
                         esp_err_to_name(cleanup_err));
            }
        }
        break;

    case NETWORK_REQUEST_START:
        if (!manager->initialized || manager->started) {
            return ESP_ERR_INVALID_STATE;
        }

        if (manager->config_valid) {
            err = wifi_manager_set_config(manager->ssid, manager->password);
            if (err != ESP_OK) {
                return err;
            }
        }

        err = wifi_manager_start();
        if (err != ESP_OK) {
            return err;
        }

        manager->started = true;
        manager->retry_pending = false;
        manager->retry_attempt = 0;

        xSemaphoreTake(manager->status_mutex, portMAX_DELAY);
        manager->status.state = NETWORK_STATE_STARTED;
        xSemaphoreGive(manager->status_mutex);

        if (manager->config_valid) {
            err = wifi_manager_connect();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Initial connect request failed: %s",
                         esp_err_to_name(err));
                network_manager_schedule_retry(manager);
            }
        }

        network_manager_post_event(NETWORK_EVENT_STARTED);
        ESP_LOGI(TAG, "Network Manager started");
        return ESP_OK;

    case NETWORK_REQUEST_STOP:
        if (!manager->initialized || !manager->started) {
            return ESP_ERR_INVALID_STATE;
        }

        manager->started = false;
        manager->retry_pending = false;

        err = wifi_manager_stop();
        if (err != ESP_OK) {
            manager->started = true;
            return err;
        }

        xSemaphoreTake(manager->status_mutex, portMAX_DELAY);
        manager->status.wifi_connected = false;
        manager->status.has_ip = false;
        manager->status.ssid[0] = '\0';
        manager->status.ip_address[0] = '\0';
        manager->status.gateway[0] = '\0';
        manager->status.state = NETWORK_STATE_STOPPED;
        xSemaphoreGive(manager->status_mutex);

        network_manager_post_event(NETWORK_EVENT_STOPPED);
        ESP_LOGI(TAG, "Network Manager stopped");
        break;

    case NETWORK_REQUEST_SET_CONFIG: {
        if (!manager->initialized || manager->started) {
            return ESP_ERR_INVALID_STATE;
        }

        const network_saved_wifi_config_t *config =
            &request->data.wifi_config;

        err = wifi_manager_set_config(config->ssid, config->password);
        if (err != ESP_OK) {
            return err;
        }

        err = network_manager_save_wifi_config(manager, config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Saving Wi-Fi configuration failed: %s",
                     esp_err_to_name(err));
            return err;
        }

        memcpy(manager->ssid, config->ssid, sizeof(manager->ssid));
        memcpy(manager->password, config->password, sizeof(manager->password));
        manager->config_valid = true;

        network_manager_post_event(NETWORK_EVENT_CONFIG_CHANGED);
        ESP_LOGI(TAG, "Wi-Fi configuration saved");
        break;
    }

    case NETWORK_REQUEST_CLEAR_CONFIG:
        if (!manager->initialized || manager->started) {
            return ESP_ERR_INVALID_STATE;
        }

        err = network_manager_erase_wifi_config(manager);
        if (err != ESP_OK) {
            return err;
        }

        memset(manager->ssid, 0, sizeof(manager->ssid));
        memset(manager->password, 0, sizeof(manager->password));
        manager->config_valid = false;

        network_manager_post_event(NETWORK_EVENT_CONFIG_CHANGED);
        ESP_LOGI(TAG, "Wi-Fi configuration erased");
        break;

    case NETWORK_REQUEST_DEINIT:
        if (!manager->initialized || manager->started) {
            return ESP_ERR_INVALID_STATE;
        }

        err = network_manager_hw_deinit(manager);
        if (err != ESP_OK) {
            return err;
        }

        manager->initialized = false;
        manager->config_valid = false;
        memset(manager->ssid, 0, sizeof(manager->ssid));
        memset(manager->password, 0, sizeof(manager->password));
        *exit_task = true;
        ESP_LOGI(TAG, "Network Manager deinitialized");
        break;

    case NETWORK_REQUEST_EXIT:
        err = network_manager_hw_deinit(manager);
        if (err == ESP_OK) {
            *exit_task = true;
        }
        break;

    default:
        return ESP_ERR_INVALID_ARG;
    }

    return err;
}

static void network_manager_try_reconnect(network_manager_t *manager)
{
    if (!manager->retry_pending ||
        (int32_t)(xTaskGetTickCount() - manager->retry_due) < 0) {
        return;
    }

    manager->retry_pending = false;

    if (!manager->started || !manager->config_valid) {
        return;
    }

    ESP_LOGI(TAG, "Attempting Wi-Fi reconnect");

    esp_err_t err = wifi_manager_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Reconnect request failed: %s", esp_err_to_name(err));
        network_manager_schedule_retry(manager);
    }
}

static void network_manager_task(void *arg)
{
    network_manager_t *manager = (network_manager_t *)arg;
    network_message_t message;

    for (;;) {
        TickType_t wait_ticks = portMAX_DELAY;

        if (manager->retry_pending) {
            TickType_t now = xTaskGetTickCount();
            if ((int32_t)(manager->retry_due - now) > 0) {
                wait_ticks = manager->retry_due - now;
            } else {
                wait_ticks = 0;
            }
        }

        if (xQueueReceive(manager->queue, &message, wait_ticks) == pdTRUE) {
            if (message.type == NETWORK_MESSAGE_REQUEST &&
                message.request != NULL) {
                bool exit_task = false;
                network_request_t *request = message.request;

                request->result = network_manager_process_request(
                    manager,
                    request,
                    &exit_task
                );

                xSemaphoreGive(request->completed);

                if (exit_task) {
                    manager->task = NULL;
                    vTaskDelete(NULL);
                }
            } else {
                network_manager_process_event(manager, &message);
            }
        }

        network_manager_try_reconnect(manager);
    }
}

static void network_manager_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    network_manager_t *manager = (network_manager_t *)arg;

    if (manager == NULL || manager->queue == NULL) {
        return;
    }

    network_message_t message = {0};

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        if (event_data == NULL) {
            return;
        }

        const wifi_event_sta_connected_t *event =
            (const wifi_event_sta_connected_t *)event_data;

        size_t length = event->ssid_len;
        if (length > sizeof(message.data.connected.ssid) - 1) {
            length = sizeof(message.data.connected.ssid) - 1;
        }

        message.type = NETWORK_MESSAGE_WIFI_CONNECTED;
        memcpy(message.data.connected.ssid, event->ssid, length);
        message.data.connected.ssid[length] = '\0';
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event =
            (const wifi_event_sta_disconnected_t *)event_data;

        message.type = NETWORK_MESSAGE_WIFI_DISCONNECTED;
        message.data.disconnected.reason = event != NULL ? event->reason : 0;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        if (event_data == NULL) {
            return;
        }

        const ip_event_got_ip_t *event =
            (const ip_event_got_ip_t *)event_data;

        message.type = NETWORK_MESSAGE_GOT_IP;
        message.data.ip_info = event->ip_info;
    } else {
        return;
    }

    if (xQueueSend(manager->queue, &message, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Network event queue full; event %ld dropped",
                 (long)event_id);
    }
}

static esp_err_t network_manager_submit(
    network_manager_t *manager,
    network_request_t *request)
{
    if (manager == NULL || manager->queue == NULL || request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    request->completed = xSemaphoreCreateBinary();
    if (request->completed == NULL) {
        return ESP_ERR_NO_MEM;
    }

    network_message_t message = {
        .type = NETWORK_MESSAGE_REQUEST,
        .request = request
    };

    if (xQueueSend(manager->queue, &message, portMAX_DELAY) != pdTRUE) {
        vSemaphoreDelete(request->completed);
        request->completed = NULL;
        return ESP_FAIL;
    }

    if (xSemaphoreTake(request->completed, portMAX_DELAY) != pdTRUE) {
        vSemaphoreDelete(request->completed);
        request->completed = NULL;
        return ESP_FAIL;
    }

    vSemaphoreDelete(request->completed);
    request->completed = NULL;
    return request->result;
}

esp_err_t network_manager_init(network_manager_handle_t *out_handle)
{
    if (out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_handle = NULL;

    network_manager_t *manager = calloc(1, sizeof(network_manager_t));
    if (manager == NULL) {
        return ESP_ERR_NO_MEM;
    }

    manager->status_mutex = xSemaphoreCreateMutex();
    if (manager->status_mutex == NULL) {
        free(manager);
        return ESP_ERR_NO_MEM;
    }

    manager->queue = xQueueCreate(
        NETWORK_MANAGER_QUEUE_LENGTH,
        sizeof(network_message_t)
    );
    if (manager->queue == NULL) {
        vSemaphoreDelete(manager->status_mutex);
        free(manager);
        return ESP_ERR_NO_MEM;
    }

    manager->status.state = NETWORK_STATE_UNINITIALIZED;

    if (xTaskCreate(
            network_manager_task,
            "network_mgr",
            NETWORK_MANAGER_TASK_STACK_SIZE,
            manager,
            NETWORK_MANAGER_TASK_PRIORITY,
            &manager->task
        ) != pdPASS) {
        vQueueDelete(manager->queue);
        vSemaphoreDelete(manager->status_mutex);
        free(manager);
        return ESP_ERR_NO_MEM;
    }

    network_request_t request = {
        .type = NETWORK_REQUEST_INIT
    };

    esp_err_t err = network_manager_submit(manager, &request);
    if (err != ESP_OK) {
        network_request_t exit_request = {
            .type = NETWORK_REQUEST_EXIT
        };
        network_manager_submit(manager, &exit_request);

        vQueueDelete(manager->queue);
        vSemaphoreDelete(manager->status_mutex);
        free(manager);
        return err;
    }

    *out_handle = manager;
    return ESP_OK;
}

esp_err_t network_manager_start(network_manager_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    network_request_t request = {
        .type = NETWORK_REQUEST_START
    };
    return network_manager_submit(handle, &request);
}

esp_err_t network_manager_stop(network_manager_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    network_request_t request = {
        .type = NETWORK_REQUEST_STOP
    };
    return network_manager_submit(handle, &request);
}

esp_err_t network_manager_deinit(network_manager_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    network_request_t request = {
        .type = NETWORK_REQUEST_DEINIT
    };

    esp_err_t err = network_manager_submit(handle, &request);
    if (err != ESP_OK) {
        return err;
    }

    vQueueDelete(handle->queue);
    vSemaphoreDelete(handle->status_mutex);
    free(handle);
    return ESP_OK;
}

esp_err_t network_manager_set_wifi_config(
    network_manager_handle_t handle,
    const char *ssid,
    const char *password)
{
    if (handle == NULL || ssid == NULL || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t ssid_len = strlen(ssid);
    size_t password_len = strlen(password);

    if (ssid_len == 0 || ssid_len > 32 || password_len > 64) {
        return ESP_ERR_INVALID_ARG;
    }

    network_request_t request = {
        .type = NETWORK_REQUEST_SET_CONFIG
    };

    memcpy(request.data.wifi_config.ssid, ssid, ssid_len);
    memcpy(request.data.wifi_config.password, password, password_len);

    return network_manager_submit(handle, &request);
}

esp_err_t network_manager_clear_wifi_config(network_manager_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    network_request_t request = {
        .type = NETWORK_REQUEST_CLEAR_CONFIG
    };
    return network_manager_submit(handle, &request);
}

esp_err_t network_manager_get_status(
    network_manager_handle_t handle,
    network_status_t *status)
{
    if (handle == NULL || status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(handle->status_mutex, portMAX_DELAY);

    if (!handle->initialized) {
        xSemaphoreGive(handle->status_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(status, &handle->status, sizeof(*status));
    xSemaphoreGive(handle->status_mutex);

    return ESP_OK;
}
