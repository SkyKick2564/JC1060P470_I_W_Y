#include "ota_manager.h"

#include <string.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if !defined(CONFIG_OTA_MANAGER_AUTH_TOKEN)
#error "OTA_MANAGER_AUTH_TOKEN must be configured"
#endif

#define OTA_HTTP_PORT          3232
#define OTA_URI                "/update"
#define OTA_AUTH_HEADER        "X-OTA-Token"
#define OTA_AUTH_TOKEN_MAX_LEN 127
#define OTA_WRITE_CHUNK_SIZE   4096

static const char *TAG = "ota_manager";
static httpd_handle_t s_server;
static const char s_auth_token[] = CONFIG_OTA_MANAGER_AUTH_TOKEN;

static bool ota_authenticate(httpd_req_t *req)
{
    size_t token_len = httpd_req_get_hdr_value_len(req, OTA_AUTH_HEADER);
    char token[OTA_AUTH_TOKEN_MAX_LEN + 1];

    if (token_len == 0 || token_len > OTA_AUTH_TOKEN_MAX_LEN) {
        return false;
    }

    if (httpd_req_get_hdr_value_str(req, OTA_AUTH_HEADER,
                                   token, sizeof(token)) != ESP_OK) {
        return false;
    }

    return strcmp(token, s_auth_token) == 0;
}

static esp_err_t ota_send_error(httpd_req_t *req,
                                const char *status,
                                const char *message)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, message);
}

static esp_err_t ota_update_handler(httpd_req_t *req)
{
    esp_err_t err;
    esp_ota_handle_t ota_handle = 0;
    const esp_partition_t *update_partition;
    uint8_t buffer[OTA_WRITE_CHUNK_SIZE];
    size_t remaining;

    if (!ota_authenticate(req)) {
        return ota_send_error(req, "401 Unauthorized",
                              "Unauthorized: invalid OTA token");
    }

    if (req->content_len < sizeof(esp_image_header_t)) {
        return ota_send_error(req, "400 Bad Request",
                              "Invalid firmware image length");
    }

    update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        return ota_send_error(req, "500 Internal Server Error",
                              "No OTA update partition available");
    }

    if (req->content_len > update_partition->size) {
        return ota_send_error(req, "413 Content Too Large",
                              "Firmware image exceeds OTA partition size");
    }

    ESP_LOGI(TAG, "Starting OTA: %u bytes -> %s",
             (unsigned)req->content_len, update_partition->label);

    err = esp_ota_begin(update_partition, req->content_len, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        return ota_send_error(req, "500 Internal Server Error", "Could not begin OTA update");
    }

    remaining = req->content_len;
    while (remaining > 0) {
        size_t chunk_size = remaining;
        int received;

        if (chunk_size > sizeof(buffer)) {
            chunk_size = sizeof(buffer);
        }

        received = httpd_req_recv(req, (char *)buffer, chunk_size);
        if (received <= 0) {
            ESP_LOGE(TAG, "Firmware receive failed: %d", received);
            esp_ota_abort(ota_handle);
            return ota_send_error(req, "500 Internal Server Error",
                                  "Firmware upload interrupted");
        }

        err = esp_ota_write(ota_handle, buffer, (size_t)received);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            return ota_send_error(req, "500 Internal Server Error",
                                  "Could not write firmware image");
        }

        remaining -= (size_t)received;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Firmware validation failed: %s", esp_err_to_name(err));
        return ota_send_error(req, HTTPD_400,
                              "Firmware image validation failed");
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not select new boot partition: %s",
                 esp_err_to_name(err));
        return ota_send_error(req, "500 Internal Server Error",
                              "Could not select new firmware");
    }

    ESP_LOGI(TAG, "OTA image accepted. Rebooting into %s",
             update_partition->label);

    httpd_resp_set_type(req, "text/plain");
    err = httpd_resp_sendstr(req, "OTA successful. Rebooting...");
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not send OTA success response: %s",
                 esp_err_to_name(err));
    }

    vTaskDelay(pdMS_TO_TICKS(700));
    esp_restart();
    return ESP_OK;
}

esp_err_t ota_manager_start(void)
{
    esp_err_t err;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (s_server != NULL) {
        return ESP_OK;
    }

    if (strlen(s_auth_token) < 16 ||
        strlen(s_auth_token) > OTA_AUTH_TOKEN_MAX_LEN) {
        ESP_LOGE(TAG,
                 "Set OTA_MANAGER_AUTH_TOKEN to a 16-127 character secret");
        return ESP_ERR_INVALID_STATE;
    }

    config.server_port = OTA_HTTP_PORT;
    config.ctrl_port = OTA_HTTP_PORT + 1;
    config.stack_size = 8192;
    config.recv_wait_timeout = 30;
    config.send_wait_timeout = 30;
    config.max_uri_handlers = 4;

    err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed: %s", esp_err_to_name(err));
        s_server = NULL;
        return err;
    }

    const httpd_uri_t update_uri = {
        .uri = OTA_URI,
        .method = HTTP_POST,
        .handler = ota_update_handler,
        .user_ctx = NULL
    };

    err = httpd_register_uri_handler(s_server, &update_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not register OTA endpoint: %s",
                 esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    ESP_LOGI(TAG, "OTA service ready: http://<device-ip>:%d%s",
             OTA_HTTP_PORT, OTA_URI);
    return ESP_OK;
}
