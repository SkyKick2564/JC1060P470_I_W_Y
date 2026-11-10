#include "network_internal.h"

#include <stdlib.h>
#include <string.h>

#include "network_interface.h"
#include "wifi_manager.h"

#include "eh_host_core.h"
#include "esp_log.h"

#define NETWORK_MANAGER_HOSTED_TIMEOUT_MS 10000

static const char *TAG = "network_manager";

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

static void network_manager_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    network_manager_t *manager = (network_manager_t *)arg;

    if (manager == NULL || manager->mutex == NULL) {
        return;
    }

    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_CONNECTED) {
        if (event_data == NULL) {
            return;
        }

        const wifi_event_sta_connected_t *event =
            (const wifi_event_sta_connected_t *)event_data;

        size_t ssid_len = event->ssid_len;
        if (ssid_len > sizeof(manager->status.ssid) - 1) {
            ssid_len = sizeof(manager->status.ssid) - 1;
        }

        xSemaphoreTake(manager->mutex, portMAX_DELAY);

        memcpy(manager->status.ssid, event->ssid, ssid_len);
        manager->status.ssid[ssid_len] = '\0';
        manager->status.wifi_connected = true;
        manager->status.state = NETWORK_STATE_WIFI_CONNECTED;

        xSemaphoreGive(manager->mutex);

        ESP_LOGI(TAG, "Wi-Fi connected");
        network_manager_post_event(NETWORK_EVENT_WIFI_CONNECTED);
        return;
    }

    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_DISCONNECTED) {
        bool had_ip;
        bool should_reconnect;

        xSemaphoreTake(manager->mutex, portMAX_DELAY);

        had_ip = manager->status.has_ip;
        should_reconnect = manager->started && manager->config_valid;

        manager->status.wifi_connected = false;
        manager->status.has_ip = false;
        manager->status.ssid[0] = '\0';
        manager->status.ip_address[0] = '\0';
        manager->status.gateway[0] = '\0';
        manager->status.state = manager->started
            ? NETWORK_STATE_DISCONNECTED
            : NETWORK_STATE_STOPPED;

        xSemaphoreGive(manager->mutex);

        if (event_data != NULL) {
            const wifi_event_sta_disconnected_t *event =
                (const wifi_event_sta_disconnected_t *)event_data;

            ESP_LOGW(TAG, "Wi-Fi disconnected, reason: %d",
                     event->reason);
        } else {
            ESP_LOGW(TAG, "Wi-Fi disconnected");
        }

        if (had_ip) {
            network_manager_post_event(NETWORK_EVENT_LOST_IP);
        }

        network_manager_post_event(NETWORK_EVENT_WIFI_DISCONNECTED);

        /*
         * esp_wifi_connect() only attempts one connection.
         * Reconnect only while the manager is running and has a config.
         * A deliberate stop sets started=false before esp_wifi_stop().
         */
        if (should_reconnect) {
            esp_err_t err = wifi_manager_connect();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Reconnect request failed: %s",
                         esp_err_to_name(err));
            }
        }
        return;
    }

    if (event_base == IP_EVENT &&
        event_id == IP_EVENT_STA_GOT_IP) {
        if (event_data == NULL) {
            return;
        }

        const ip_event_got_ip_t *event =
            (const ip_event_got_ip_t *)event_data;

        xSemaphoreTake(manager->mutex, portMAX_DELAY);

        manager->status.wifi_connected = true;
        manager->status.has_ip = true;
        manager->status.state = NETWORK_STATE_IP_READY;

        esp_ip4addr_ntoa(
            &event->ip_info.ip,
            manager->status.ip_address,
            sizeof(manager->status.ip_address)
        );

        esp_ip4addr_ntoa(
            &event->ip_info.gw,
            manager->status.gateway,
            sizeof(manager->status.gateway)
        );

        xSemaphoreGive(manager->mutex);

        ESP_LOGI(TAG, "Got IP: %s", manager->status.ip_address);
        ESP_LOGI(TAG, "Gateway: %s", manager->status.gateway);

        network_manager_post_event(NETWORK_EVENT_GOT_IP);
    }
}

esp_err_t network_manager_init(network_manager_handle_t *out_handle)
{
    if (out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_handle = NULL;

    int hosted_result = eh_host_wait_auto_init_ready(
        NETWORK_MANAGER_HOSTED_TIMEOUT_MS
    );

    if (hosted_result != 0) {
        ESP_LOGE(TAG, "ESP-Hosted initialization failed: %d",
                 hosted_result);
        return ESP_FAIL;
    }

    network_manager_t *manager = calloc(1, sizeof(network_manager_t));
    if (manager == NULL) {
        return ESP_ERR_NO_MEM;
    }

    manager->mutex = xSemaphoreCreateMutex();
    if (manager->mutex == NULL) {
        free(manager);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = network_interface_init_sta(&manager->sta_netif);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "STA interface initialization failed: %s",
                 esp_err_to_name(err));
        vSemaphoreDelete(manager->mutex);
        free(manager);
        return err;
    }

    err = wifi_manager_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi initialization failed: %s",
                 esp_err_to_name(err));
        network_interface_deinit_sta(manager->sta_netif);
        vSemaphoreDelete(manager->mutex);
        free(manager);
        return err;
    }

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
        wifi_manager_deinit();
        network_interface_deinit_sta(manager->sta_netif);
        vSemaphoreDelete(manager->mutex);
        free(manager);
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

        esp_event_handler_instance_unregister(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            manager->wifi_event_instance
        );

        wifi_manager_deinit();
        network_interface_deinit_sta(manager->sta_netif);
        vSemaphoreDelete(manager->mutex);
        free(manager);
        return err;
    }

    manager->status.state = NETWORK_STATE_INITIALIZED;
    manager->initialized = true;

    *out_handle = manager;

    ESP_LOGI(TAG, "Network Manager initialized");
    return ESP_OK;
}

esp_err_t network_manager_start(network_manager_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    network_manager_t *manager = handle;
    char ssid[sizeof(manager->ssid)];
    char password[sizeof(manager->password)];
    bool config_valid;

    xSemaphoreTake(manager->mutex, portMAX_DELAY);

    if (!manager->initialized || manager->started) {
        xSemaphoreGive(manager->mutex);
        return ESP_ERR_INVALID_STATE;
    }

    config_valid = manager->config_valid;
    memcpy(ssid, manager->ssid, sizeof(ssid));
    memcpy(password, manager->password, sizeof(password));

    xSemaphoreGive(manager->mutex);

    if (config_valid) {
        esp_err_t err = wifi_manager_set_config(ssid, password);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Wi-Fi configuration failed: %s",
                     esp_err_to_name(err));
            return err;
        }
    }

    esp_err_t err = wifi_manager_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi start failed: %s", esp_err_to_name(err));
        return err;
    }

    xSemaphoreTake(manager->mutex, portMAX_DELAY);
    manager->started = true;
    manager->status.state = NETWORK_STATE_STARTED;
    xSemaphoreGive(manager->mutex);

    if (config_valid) {
        err = wifi_manager_connect();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Wi-Fi connect request failed: %s",
                     esp_err_to_name(err));

            xSemaphoreTake(manager->mutex, portMAX_DELAY);
            manager->started = false;
            manager->status.state = NETWORK_STATE_INITIALIZED;
            xSemaphoreGive(manager->mutex);

            esp_err_t stop_err = wifi_manager_stop();
            if (stop_err != ESP_OK) {
                ESP_LOGE(TAG, "Wi-Fi rollback failed: %s",
                         esp_err_to_name(stop_err));
            }
            return err;
        }
    }

    ESP_LOGI(TAG, "Network Manager started");
    network_manager_post_event(NETWORK_EVENT_STARTED);
    return ESP_OK;
}

esp_err_t network_manager_stop(network_manager_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    network_manager_t *manager = handle;

    xSemaphoreTake(manager->mutex, portMAX_DELAY);

    if (!manager->initialized || !manager->started) {
        xSemaphoreGive(manager->mutex);
        return ESP_ERR_INVALID_STATE;
    }

    /* Prevent the disconnect event from triggering an automatic reconnect. */
    manager->started = false;
    xSemaphoreGive(manager->mutex);

    esp_err_t err = wifi_manager_stop();
    if (err != ESP_OK) {
        xSemaphoreTake(manager->mutex, portMAX_DELAY);
        manager->started = true;
        xSemaphoreGive(manager->mutex);

        ESP_LOGE(TAG, "Wi-Fi stop failed: %s", esp_err_to_name(err));
        return err;
    }

    xSemaphoreTake(manager->mutex, portMAX_DELAY);
    manager->status.wifi_connected = false;
    manager->status.has_ip = false;
    manager->status.ssid[0] = '\0';
    manager->status.ip_address[0] = '\0';
    manager->status.gateway[0] = '\0';
    manager->status.state = NETWORK_STATE_STOPPED;
    xSemaphoreGive(manager->mutex);

    ESP_LOGI(TAG, "Network Manager stopped");
    network_manager_post_event(NETWORK_EVENT_STOPPED);
    return ESP_OK;
}

esp_err_t network_manager_deinit(network_manager_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    network_manager_t *manager = handle;

    xSemaphoreTake(manager->mutex, portMAX_DELAY);
    bool can_deinit = manager->initialized && !manager->started;
    xSemaphoreGive(manager->mutex);

    if (!can_deinit) {
        return ESP_ERR_INVALID_STATE;
    }

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

    esp_err_t err = wifi_manager_deinit();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi deinitialization failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    network_interface_deinit_sta(manager->sta_netif);
    manager->sta_netif = NULL;

    memset(manager->password, 0, sizeof(manager->password));
    manager->config_valid = false;
    manager->initialized = false;

    vSemaphoreDelete(manager->mutex);
    free(manager);

    ESP_LOGI(TAG, "Network Manager deinitialized");
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

    network_manager_t *manager = handle;

    xSemaphoreTake(manager->mutex, portMAX_DELAY);

    if (!manager->initialized || manager->started) {
        xSemaphoreGive(manager->mutex);
        return ESP_ERR_INVALID_STATE;
    }

    memset(manager->ssid, 0, sizeof(manager->ssid));
    memset(manager->password, 0, sizeof(manager->password));

    memcpy(manager->ssid, ssid, ssid_len);
    memcpy(manager->password, password, password_len);
    manager->config_valid = true;

    xSemaphoreGive(manager->mutex);

    ESP_LOGI(TAG, "Wi-Fi configuration updated");
    network_manager_post_event(NETWORK_EVENT_CONFIG_CHANGED);
    return ESP_OK;
}

esp_err_t network_manager_clear_wifi_config(network_manager_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    network_manager_t *manager = handle;

    xSemaphoreTake(manager->mutex, portMAX_DELAY);

    if (!manager->initialized || manager->started) {
        xSemaphoreGive(manager->mutex);
        return ESP_ERR_INVALID_STATE;
    }

    memset(manager->ssid, 0, sizeof(manager->ssid));
    memset(manager->password, 0, sizeof(manager->password));
    manager->config_valid = false;

    xSemaphoreGive(manager->mutex);

    ESP_LOGI(TAG, "Wi-Fi configuration cleared");
    network_manager_post_event(NETWORK_EVENT_CONFIG_CHANGED);
    return ESP_OK;
}

esp_err_t network_manager_get_status(
    network_manager_handle_t handle,
    network_status_t *status)
{
    if (handle == NULL || status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    network_manager_t *manager = handle;

    xSemaphoreTake(manager->mutex, portMAX_DELAY);
    if (!manager->initialized) {
        xSemaphoreGive(manager->mutex);
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(status, &manager->status, sizeof(*status));
    xSemaphoreGive(manager->mutex);

    return ESP_OK;
}
