#include "internet_radio.h"

#include "audio_manager.h"
#include "network_manager.h"
#include "esp_event.h"
#include "esp_log.h"

static const char *TAG = "internet_radio";
static const char *STREAM_URL =
    "https://ice5.somafm.com/thetrip-128-mp3";

static bool s_initialized;

static void internet_radio_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_data;

    if (event_id == NETWORK_EVENT_INTERNET_CONNECTED) {
        esp_err_t err = audio_manager_play_url(STREAM_URL);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Internet Radio stream queued");
        } else if (err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Queue stream failed: %s",
                     esp_err_to_name(err));
        } else {
            ESP_LOGW(TAG, "Audio Manager is busy; stream was not queued");
        }
    } else if (event_id == NETWORK_EVENT_INTERNET_DISCONNECTED) {
        esp_err_t err = audio_manager_stop();
        if (err == ESP_OK) {
            ESP_LOGW(TAG, "Internet lost; stopping stream");
        } else if (err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Stop stream failed: %s",
                     esp_err_to_name(err));
        }
    }
}

esp_err_t internet_radio_init(void)
{
    esp_err_t err;

    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!audio_manager_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    err = esp_event_handler_register(
        NETWORK_MANAGER_EVENT,
        NETWORK_EVENT_INTERNET_CONNECTED,
        internet_radio_event_handler,
        NULL
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Register connected event failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    err = esp_event_handler_register(
        NETWORK_MANAGER_EVENT,
        NETWORK_EVENT_INTERNET_DISCONNECTED,
        internet_radio_event_handler,
        NULL
    );
    if (err != ESP_OK) {
        esp_event_handler_unregister(
            NETWORK_MANAGER_EVENT,
            NETWORK_EVENT_INTERNET_CONNECTED,
            internet_radio_event_handler
        );
        ESP_LOGE(TAG, "Register disconnected event failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Subscribed to Network Manager Internet events");
    return ESP_OK;
}

esp_err_t internet_radio_deinit(void)
{
    esp_err_t connected_err;
    esp_err_t disconnected_err;

    if (!s_initialized) {
        return ESP_OK;
    }

    connected_err = esp_event_handler_unregister(
        NETWORK_MANAGER_EVENT,
        NETWORK_EVENT_INTERNET_CONNECTED,
        internet_radio_event_handler
    );
    disconnected_err = esp_event_handler_unregister(
        NETWORK_MANAGER_EVENT,
        NETWORK_EVENT_INTERNET_DISCONNECTED,
        internet_radio_event_handler
    );

    if (connected_err != ESP_OK || disconnected_err != ESP_OK) {
        ESP_LOGE(TAG, "Unregister events failed: connected=%s disconnected=%s",
                 esp_err_to_name(connected_err),
                 esp_err_to_name(disconnected_err));
        return connected_err != ESP_OK ? connected_err : disconnected_err;
    }

    s_initialized = false;
    ESP_LOGI(TAG, "Unsubscribed from Network Manager Internet events");
    return ESP_OK;
}
