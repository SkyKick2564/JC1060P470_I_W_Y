#include <stdbool.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_err.h"
#include "network_manager.h"
#include "ota_manager.h"
#include "app_config.h"

static const char *TAG = "app";
static wifi_ap_record_t s_ap_records[NETWORK_MANAGER_MAX_SCAN_RESULTS];

/*
 * Called when Network Manager completes a Wi-Fi scan.
 */
static void on_wifi_scan_updated(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    network_manager_handle_t network =
        (network_manager_handle_t)arg;
    uint16_t count = 0;
    uint16_t total_found = 0;
    (void)event_base;
    (void)event_id;
    (void)event_data;

    esp_err_t err = network_manager_get_scan_results(
        network,
        s_ap_records,
        NETWORK_MANAGER_MAX_SCAN_RESULTS,
        &count,
        &total_found
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Get scan results failed: %s",
                 esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "========== Wi-Fi Scan Results ==========");
    ESP_LOGI(TAG, "Total APs found: %u", (unsigned)total_found);
    ESP_LOGI(TAG, "Cached APs: %u", (unsigned)count);

    for (uint16_t i = 0; i < count; i++) {
        ESP_LOGI(TAG,
                 "[%u] SSID: %.*s | RSSI: %d dBm | Channel: %u",
                 (unsigned)(i + 1),
                 (int)sizeof(s_ap_records[i].ssid),
                 (const char *)s_ap_records[i].ssid,
                 (int)s_ap_records[i].rssi,
                 (unsigned)s_ap_records[i].primary);
    }

    ESP_LOGI(TAG, "========================================");
}

static esp_err_t app_before_update(void *user_ctx)
{
    // เตรียม Application ก่อน OTA

    return ESP_OK;
}

void app_main(void)
{
    network_manager_handle_t network = NULL;
    esp_err_t err;
    bool ota_initialized = false;

    err = network_manager_init(&network);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Network Manager init failed: %s",
                 esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Network Manager initialized");

    err = ota_manager_init(network, &APP_OTA_CONFIG);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA Manager init failed; networking will continue: %s",
                 esp_err_to_name(err));
    } else {
        ota_initialized = true;
        ESP_LOGI(TAG, "OTA Manager initialized");
    }

    // err = network_manager_set_wifi_config(
    //     network,
    //     "PoomChalermpol_2.4G",
    //     "pomchalermpol123"
    // );

    // if (err != ESP_OK) {
    //     ESP_LOGE(TAG, "Wi-Fi config failed: %s", esp_err_to_name(err));
    //     return;
    // }

    err = esp_event_handler_register(
        NETWORK_MANAGER_EVENT,
        NETWORK_EVENT_SCAN_UPDATED,
        on_wifi_scan_updated,
        network
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi scan event registration failed: %s",
                 esp_err_to_name(err));
        return;
    }

    err = network_manager_start(network);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Network Manager start failed: %s",
                 esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Network Manager started");

    /* Existing Wi-Fi scan test harness; complete app startup before OTA starts. */
    err = network_manager_start_scan(network);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Start scan failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Wi-Fi scanning enabled");

    /*
     * Start OTA last. OTA Manager performs pending-image confirmation
     * internally, after the application's critical startup steps succeeded.
     */
    if (ota_initialized) {
        err = ota_manager_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA Manager start failed; networking will continue: %s",
                     esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "OTA Manager started");
        }
    }
}
