#include <stdio.h>

#include "esp_log.h"
#include "esp_err.h"

#include "network_manager.h"

static const char *TAG = "app";

void app_main(void)
{
    network_manager_handle_t network = NULL;
    esp_err_t err;

    err = network_manager_init(&network);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Init failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Network Manager initialized");

    err = network_manager_set_wifi_config(
        network,
        "iPhone Pom",
        "Pom123456"
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi config failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Wi-Fi configuration set");

    err = network_manager_start(network);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Start failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Network Manager started");
}