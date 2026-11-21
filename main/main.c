#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_err.h"
#include "network_manager.h"
#include "ota_manager.h"
#include "app_config.h"
#include "esp_board_manager.h"
#include "app.h"
#include "ui.h"

static const char *TAG = "MAIN";

static esp_err_t app_before_update(void *user_ctx)
{
    esp_err_t err;
    network_manager_handle_t network =
        (network_manager_handle_t)user_ctx;

    ESP_LOGW(TAG, "OTA: Application pre-flash callback");

    /* Deinit performs STOP internally, then tears down Wi-Fi and ESP-Hosted. */
    err = network_manager_deinit(network);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Network Manager deinit failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Network Manager and ESP-Hosted deinitialized");

    /* LVGL can be deinitialized only after all media tasks have exited. */
    err = app_lvgl_deinit();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LVGL deinit failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    err = esp_board_manager_deinit();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Board Manager deinit failed: %s",
                 esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Board Manager deinitialized");
    return ESP_OK;
}

static void app_update_available(
    const ota_firmware_info_t *info,
    void *user_ctx)
{
    (void)user_ctx;

    ESP_LOGW(TAG, "OTA: Update available");
    ESP_LOGI(TAG, "  Version: %s", info->version);
    ESP_LOGI(TAG, "  Description: %s",
             info->description[0] != '\0' ? info->description : "(none)");
    ESP_LOGI(TAG, "  Release date: %s",
             info->release_date[0] != '\0' ? info->release_date : "(none)");
    ESP_LOGI(TAG, "  Size: %u bytes", (unsigned)info->size);
}

void app_main(void)
{
    network_manager_handle_t network = NULL;
    esp_err_t err;

    // 1. Initialize Board Hardware
    err = esp_board_manager_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Board Manager init failed: %s",
                 esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Board Manager initialized");

    /* Initialize application and EEZ UI before registering the video target. */
    err = app_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Application initialization failed: %s",
                 esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Application started");

    /*
     * Confirm a pending OTA image as soon as the board and essential UI
     * initialization have succeeded. Do not make rollback confirmation
     * depend on optional SD/media playback or later network startup.
     */
    ota_manager_confirm_running_firmware();

    err = network_manager_init(&network);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Network Manager init failed: %s",
                 esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Network Manager initialized");

    ota_manager_config_t ota_config = APP_OTA_CONFIG;
    ota_config.user_ctx = network;
    err = ota_manager_init(network, &ota_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA Manager init failed; networking will continue: %s",
                 esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "OTA Manager initialized");
        err = ota_manager_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA Manager start failed: %s",
                    esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "OTA Manager started");
        }
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

    err = network_manager_start(network);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Network Manager start failed: %s",
                 esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Network Manager started");
}
