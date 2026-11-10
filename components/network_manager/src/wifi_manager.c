#include "wifi_manager.h"

#include <string.h>

#include "esp_wifi.h"

#define WIFI_MANAGER_MAX_SCAN_RESULTS 20

static bool s_wifi_initialized = false;
static bool s_wifi_started = false;

static wifi_ap_record_t s_scan_records[WIFI_MANAGER_MAX_SCAN_RESULTS];
static uint16_t s_scan_count = 0;
static uint16_t s_scan_total_found = 0;

esp_err_t wifi_manager_init(void)
{
    if (s_wifi_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();

    esp_err_t err = esp_wifi_init(&config);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        esp_wifi_deinit();
        return err;
    }

    s_wifi_initialized = true;
    return ESP_OK;
}

esp_err_t wifi_manager_set_config(
    const char *ssid,
    const char *password)
{
    if (!s_wifi_initialized || ssid == NULL || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t ssid_len = strlen(ssid);
    size_t password_len = strlen(password);

    if (ssid_len == 0 || ssid_len > 32 || password_len > 64) {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * Keep the current project's compatibility setting: 802.11 b/g/n/ax.
     * Return the error instead of aborting the whole application.
     */
    esp_err_t err = esp_wifi_set_protocol(
        WIFI_IF_STA,
        WIFI_PROTOCOL_11B |
        WIFI_PROTOCOL_11G |
        WIFI_PROTOCOL_11N |
        WIFI_PROTOCOL_11AX
    );
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t config = {0};

    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    config.sta.channel = 0;
    config.sta.bssid_set = false;

    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;

    config.sta.rm_enabled = false;
    config.sta.btm_enabled = false;
    config.sta.mbo_enabled = false;
    config.sta.ft_enabled = false;

    config.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;

    memcpy(config.sta.ssid, ssid, ssid_len);
    memcpy(config.sta.password, password, password_len);

    return esp_wifi_set_config(WIFI_IF_STA, &config);
}

esp_err_t wifi_manager_start(void)
{
    if (!s_wifi_initialized || s_wifi_started) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_wifi_start();
    if (err == ESP_OK) {
        s_wifi_started = true;
    }

    return err;
}

esp_err_t wifi_manager_connect(void)
{
    if (!s_wifi_started) {
        return ESP_ERR_INVALID_STATE;
    }

    return esp_wifi_connect();
}

esp_err_t wifi_manager_disconnect(void)
{
    if (!s_wifi_started) {
        return ESP_ERR_INVALID_STATE;
    }

    return esp_wifi_disconnect();
}

esp_err_t wifi_manager_stop(void)
{
    if (!s_wifi_started) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_wifi_stop();
    if (err == ESP_OK) {
        s_wifi_started = false;
    }

    return err;
}

esp_err_t wifi_manager_deinit(void)
{
    if (!s_wifi_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_wifi_started) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_wifi_deinit();
    if (err == ESP_OK) {
        s_wifi_initialized = false;
        memset(s_scan_records, 0, sizeof(s_scan_records));
        s_scan_count = 0;
        s_scan_total_found = 0;
    }

    return err;
}

esp_err_t wifi_manager_scan(void)
{
    if (!s_wifi_initialized || !s_wifi_started) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(s_scan_records, 0, sizeof(s_scan_records));
    s_scan_count = 0;
    s_scan_total_found = 0;

    wifi_scan_config_t scan_config = {0};
    scan_config.channel = 0;
    scan_config.show_hidden = false;
    scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_scan_get_ap_num(&s_scan_total_found);
    if (err != ESP_OK) {
        s_scan_total_found = 0;
        return err;
    }

    if (s_scan_total_found == 0) {
        return ESP_OK;
    }

    uint16_t count = s_scan_total_found;
    if (count > WIFI_MANAGER_MAX_SCAN_RESULTS) {
        count = WIFI_MANAGER_MAX_SCAN_RESULTS;
    }

    err = esp_wifi_scan_get_ap_records(&count, s_scan_records);
    if (err != ESP_OK) {
        memset(s_scan_records, 0, sizeof(s_scan_records));
        s_scan_total_found = 0;
        return err;
    }

    s_scan_count = count;
    return ESP_OK;
}

const wifi_ap_record_t *wifi_manager_get_scan_results(
    uint16_t *count,
    uint16_t *total_found)
{
    if (count != NULL) {
        *count = s_scan_count;
    }

    if (total_found != NULL) {
        *total_found = s_scan_total_found;
    }

    return s_scan_records;
}
