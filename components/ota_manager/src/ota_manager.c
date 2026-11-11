#include "ota_manager.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "psa/crypto.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define OTA_TASK_STACK_SIZE       16384
#define OTA_TASK_PRIORITY         4
#define OTA_NO_NETWORK_RETRY_MS   5000
#define OTA_HTTP_TIMEOUT_MS       15000
#define OTA_MANIFEST_MAX_SIZE     4096
#define OTA_DOWNLOAD_CHUNK_SIZE   4096
#define OTA_TOKEN_MAX_LEN         127
#define OTA_MANIFEST_URL_MAX_LEN  512
#define OTA_SHA256_HEX_LEN        64

static const char *TAG = "ota_manager";
static network_manager_handle_t s_network;
static TaskHandle_t s_task;
static char s_manifest_url[OTA_MANIFEST_URL_MAX_LEN + 1];
static char s_auth_token[OTA_TOKEN_MAX_LEN + 1];
static uint32_t s_check_interval_sec;

typedef struct {
    char project[48];
    char version[32];
    char firmware_url[512];
    char sha256[OTA_SHA256_HEX_LEN + 1];
    size_t size;
} ota_manifest_t;

static bool ota_parse_version(const char *version, uint32_t parts[4])
{
    const char *cursor = version;
    unsigned int count = 0;

    if (version == NULL || version[0] == '\0') {
        return false;
    }

    memset(parts, 0, sizeof(uint32_t) * 4);

    while (*cursor != '\0') {
        char *end = NULL;
        unsigned long value;

        if (count >= 4 || *cursor < '0' || *cursor > '9') {
            return false;
        }

        value = strtoul(cursor, &end, 10);
        if (end == cursor || value > UINT32_MAX) {
            return false;
        }

        parts[count++] = (uint32_t)value;
        cursor = end;

        if (*cursor == '\0') {
            break;
        }
        if (*cursor != '.') {
            return false;
        }
        cursor++;
    }

    return count >= 2;
}

static bool ota_version_is_newer(const char *remote, const char *current)
{
    uint32_t remote_parts[4];
    uint32_t current_parts[4];

    if (!ota_parse_version(remote, remote_parts) ||
        !ota_parse_version(current, current_parts)) {
        ESP_LOGE(TAG, "Invalid firmware version (remote=%s current=%s)",
                 remote, current);
        return false;
    }

    for (size_t i = 0; i < 4; i++) {
        if (remote_parts[i] > current_parts[i]) {
            return true;
        }
        if (remote_parts[i] < current_parts[i]) {
            return false;
        }
    }

    return false;
}

static bool ota_copy_json_string(cJSON *object, const char *key,
                                 char *destination, size_t capacity)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }

    size_t length = strlen(item->valuestring);
    if (length == 0 || length >= capacity) {
        return false;
    }

    memcpy(destination, item->valuestring, length + 1);
    return true;
}

static bool ota_parse_manifest(const char *json, ota_manifest_t *manifest)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        ESP_LOGE(TAG, "Manifest JSON is invalid");
        return false;
    }

    cJSON *size_item = cJSON_GetObjectItemCaseSensitive(root, "size");
    bool valid =
        ota_copy_json_string(root, "project", manifest->project,
                             sizeof(manifest->project)) &&
        ota_copy_json_string(root, "version", manifest->version,
                             sizeof(manifest->version)) &&
        ota_copy_json_string(root, "firmware_url", manifest->firmware_url,
                             sizeof(manifest->firmware_url)) &&
        ota_copy_json_string(root, "sha256", manifest->sha256,
                             sizeof(manifest->sha256)) &&
        cJSON_IsNumber(size_item) &&
        size_item->valuedouble > 0 &&
        size_item->valuedouble <= (double)SIZE_MAX &&
        strlen(manifest->sha256) == OTA_SHA256_HEX_LEN;

    if (valid) {
        manifest->size = (size_t)size_item->valuedouble;
        uint32_t parsed[4];
        valid = ota_parse_version(manifest->version, parsed);
        for (size_t i = 0; valid && i < OTA_SHA256_HEX_LEN; i++) {
            char c = manifest->sha256[i];
            valid = (c >= '0' && c <= '9') ||
                    (c >= 'a' && c <= 'f') ||
                    (c >= 'A' && c <= 'F');
        }
    }

    cJSON_Delete(root);
    return valid;
}

static esp_err_t ota_http_open_get(const char *url,
                                   esp_http_client_handle_t *out_client,
                                   int64_t *out_content_length,
                                   bool *out_server_reachable)
{
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = OTA_HTTP_TIMEOUT_MS,
        .buffer_size = OTA_DOWNLOAD_CHUNK_SIZE,
        .buffer_size_tx = 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
        /*
         * GitHub Releases redirect downloads to release-assets.githubusercontent.com.
         * Handle redirects explicitly because this component uses the streaming
         * open/fetch_headers/read API rather than esp_http_client_perform().
         */
        .disable_auto_redirect = true,
        .max_redirection_count = 5,
        .keep_alive_enable = false
    };

    if (out_server_reachable != NULL) {
        *out_server_reachable = false;
    }

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = ESP_OK;
    if (s_auth_token[0] != '\0') {
        err = esp_http_client_set_header(
            client, "X-OTA-Token", s_auth_token);
    }
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    int64_t content_length = -1;
    int status = 0;
    bool response_received = false;

    for (int redirect = 0; redirect <= 5; redirect++) {
        err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "HTTP connection failed: %s", esp_err_to_name(err));
            break;
        }

        content_length = esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        if (status > 0) {
            response_received = true;
            if (out_server_reachable != NULL) {
                *out_server_reachable = true;
            }
        }

        bool is_redirect = status == 301 || status == 302 || status == 303 ||
                           status == 307 || status == 308;
        if (!is_redirect) {
            break;
        }

        if (redirect == 5) {
            ESP_LOGE(TAG, "HTTP redirect limit exceeded");
            err = ESP_FAIL;
            break;
        }

        err = esp_http_client_set_redirection(client);
        esp_http_client_close(client);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Could not follow HTTP redirect: %s",
                     esp_err_to_name(err));
            break;
        }
    }

    if (err != ESP_OK || status != 200 || content_length < 0) {
        if (response_received) {
            ESP_LOGE(TAG, "HTTP GET failed: status=%d length=%lld",
                     status, (long long)content_length);
        }
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return err != ESP_OK ? err : ESP_FAIL;
    }

    *out_client = client;
    *out_content_length = content_length;
    return ESP_OK;
}

static esp_err_t ota_fetch_manifest(ota_manifest_t *manifest,
                                    bool *out_server_reachable)
{
    esp_http_client_handle_t client = NULL;
    int64_t content_length = 0;
    char manifest_url[OTA_MANIFEST_URL_MAX_LEN + 40];
    int url_length = snprintf(
        manifest_url, sizeof(manifest_url), "%s%cota_cache_bust=%lld",
        s_manifest_url,
        strchr(s_manifest_url, '?') != NULL ? '&' : '?',
        (long long)esp_timer_get_time());

    if (url_length < 0 || (size_t)url_length >= sizeof(manifest_url)) {
        ESP_LOGE(TAG, "Could not build cache-busted manifest URL");
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = ota_http_open_get(
        manifest_url, &client, &content_length, out_server_reachable);
    if (err != ESP_OK) {
        return err;
    }

    if (content_length <= 0 || content_length > OTA_MANIFEST_MAX_SIZE) {
        ESP_LOGE(TAG, "Manifest size is invalid: %lld",
                 (long long)content_length);
        err = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    char *json = calloc(1, (size_t)content_length + 1);
    if (json == NULL) {
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    size_t received_total = 0;
    while (received_total < (size_t)content_length) {
        int received = esp_http_client_read(
            client,
            json + received_total,
            (int)((size_t)content_length - received_total));
        if (received <= 0) {
            err = ESP_FAIL;
            break;
        }
        received_total += (size_t)received;
    }

    if (err == ESP_OK &&
        !ota_parse_manifest(json, manifest)) {
        err = ESP_ERR_INVALID_RESPONSE;
    }

    free(json);

cleanup:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return err;
}

static int ota_hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool ota_sha256_matches(const unsigned char digest[32],
                               const char *expected_hex)
{
    static const char hex[] = "0123456789abcdef";
    char actual[OTA_SHA256_HEX_LEN + 1];

    for (size_t i = 0; i < 32; i++) {
        actual[i * 2] = hex[digest[i] >> 4];
        actual[i * 2 + 1] = hex[digest[i] & 0x0f];
    }
    actual[OTA_SHA256_HEX_LEN] = '\0';

    for (size_t i = 0; i < OTA_SHA256_HEX_LEN; i++) {
        int expected = ota_hex_value(expected_hex[i]);
        int actual_value = ota_hex_value(actual[i]);
        if (expected < 0 || expected != actual_value) {
            return false;
        }
    }
    return true;
}

static esp_err_t ota_download_and_install(const ota_manifest_t *manifest)
{
    const esp_partition_t *update_partition =
        esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL || manifest->size > update_partition->size) {
        ESP_LOGE(TAG, "No suitable OTA partition for %u bytes",
                 (unsigned)manifest->size);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_http_client_handle_t client = NULL;
    int64_t content_length = 0;
    esp_err_t err = ota_http_open_get(
        manifest->firmware_url, &client, &content_length, NULL);
    if (err != ESP_OK) {
        return err;
    }

    if (content_length <= 0 ||
        (uint64_t)content_length != (uint64_t)manifest->size) {
        ESP_LOGE(TAG, "Firmware length mismatch: manifest=%u HTTP=%lld",
                 (unsigned)manifest->size, (long long)content_length);
        err = ESP_ERR_INVALID_SIZE;
        goto cleanup_http;
    }

    esp_ota_handle_t ota_handle = 0;
    err = esp_ota_begin(update_partition, manifest->size, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        goto cleanup_http;
    }

    psa_hash_operation_t sha = PSA_HASH_OPERATION_INIT;
    psa_status_t psa_status = psa_crypto_init();
    if (psa_status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "PSA crypto initialization failed: %d",
                 (int)psa_status);
        err = ESP_FAIL;
        goto abort_ota;
    }

    psa_status = psa_hash_setup(&sha, PSA_ALG_SHA_256);
    if (psa_status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "PSA SHA-256 setup failed: %d", (int)psa_status);
        err = ESP_FAIL;
        goto abort_ota;
    }

    uint8_t buffer[OTA_DOWNLOAD_CHUNK_SIZE];
    size_t downloaded = 0;

    while (downloaded < manifest->size) {
        size_t remaining = manifest->size - downloaded;
        size_t requested = remaining < sizeof(buffer)
            ? remaining : sizeof(buffer);
        int received = esp_http_client_read(
            client, (char *)buffer, (int)requested);

        if (received <= 0) {
            ESP_LOGE(TAG, "Firmware download interrupted: %d", received);
            err = ESP_FAIL;
            goto abort_ota;
        }

        err = esp_ota_write(ota_handle, buffer, (size_t)received);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            goto abort_ota;
        }

        psa_status = psa_hash_update(&sha, buffer, (size_t)received);
        if (psa_status != PSA_SUCCESS) {
            ESP_LOGE(TAG, "PSA SHA-256 update failed: %d", (int)psa_status);
            err = ESP_FAIL;
            goto abort_ota;
        }

        downloaded += (size_t)received;
        ESP_LOGI(TAG, "Firmware download: %u/%u bytes",
                 (unsigned)downloaded, (unsigned)manifest->size);
    }

    uint8_t digest[32];
    size_t digest_length = 0;
    psa_status = psa_hash_finish(
        &sha, digest, sizeof(digest), &digest_length);
    if (psa_status != PSA_SUCCESS || digest_length != sizeof(digest)) {
        ESP_LOGE(TAG, "PSA SHA-256 finish failed: %d", (int)psa_status);
        err = ESP_FAIL;
        goto abort_ota;
    }

    if (!ota_sha256_matches(digest, manifest->sha256)) {
        ESP_LOGE(TAG, "Firmware SHA-256 does not match manifest");
        esp_ota_abort(ota_handle);
        err = ESP_ERR_INVALID_CRC;
        goto cleanup_http;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Firmware image validation failed: %s",
                 esp_err_to_name(err));
        goto cleanup_http;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not select new boot partition: %s",
                 esp_err_to_name(err));
        goto cleanup_http;
    }

    ESP_LOGW(TAG, "Firmware %s validated; rebooting into %s",
             manifest->version, update_partition->label);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    vTaskDelay(pdMS_TO_TICKS(700));
    esp_restart();
    return ESP_OK;

abort_ota:
    psa_hash_abort(&sha);
    esp_ota_abort(ota_handle);
cleanup_http:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return err;
}

static void ota_check_for_update(bool *out_server_reachable)
{
    ota_manifest_t manifest = {0};
    const esp_app_desc_t *running_app = esp_app_get_description();

    if (out_server_reachable != NULL) {
        *out_server_reachable = false;
    }

    esp_err_t err = ota_fetch_manifest(&manifest, out_server_reachable);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not read firmware manifest: %s",
                 esp_err_to_name(err));
        return;
    }

    if (strcmp(manifest.project, "JC1060P470_I_W_Y") != 0) {
        ESP_LOGE(TAG, "Manifest is for another project: %s", manifest.project);
        return;
    }

    ESP_LOGI(TAG, "Firmware version: running=%s available=%s",
             running_app->version, manifest.version);

    if (!ota_version_is_newer(manifest.version, running_app->version)) {
        ESP_LOGI(TAG, "Firmware is up to date");
        return;
    }

    ESP_LOGW(TAG, "New firmware found; downloading %s", manifest.version);
    err = ota_download_and_install(&manifest);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA update failed: %s", esp_err_to_name(err));
    }
}

/*
 * Confirm the running image only after the application has completed its
 * startup sequence. If it is still pending verification, a reboot before
 * this point will allow the bootloader to roll back to the previous image.
 */
static void ota_confirm_running_firmware_if_pending(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL) {
        ESP_LOGE(TAG, "Could not get running OTA partition");
        return;
    }

    esp_ota_img_states_t state;
    esp_err_t err = esp_ota_get_state_partition(running, &state);

    if (err == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        err = esp_ota_mark_app_valid_cancel_rollback();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Could not confirm running firmware: %s",
                     esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Running firmware confirmed; rollback cancelled");
        }
    } else if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Could not read OTA image state: %s",
                 esp_err_to_name(err));
    }
}

static void ota_manager_task(void *arg)
{
    (void)arg;
    const TickType_t check_interval =
        pdMS_TO_TICKS(s_check_interval_sec * 1000U);
    bool endpoint_was_reachable = false;

    ESP_LOGI(TAG, "OTA task started; manifest: %s",
             s_manifest_url);

    for (;;) {
        network_status_t status = {0};
        esp_err_t err = network_manager_get_status(s_network, &status);

        if (err == ESP_OK && status.has_ip) {
            bool endpoint_reachable = false;
            ota_check_for_update(&endpoint_reachable);

            if (endpoint_reachable && !endpoint_was_reachable) {
                ESP_LOGI(TAG, "Internet available (GitHub OTA manifest responded)");
            } else if (!endpoint_reachable && endpoint_was_reachable) {
                ESP_LOGW(TAG, "Internet unavailable (GitHub OTA endpoint unreachable)");
            }
            endpoint_was_reachable = endpoint_reachable;
            vTaskDelay(check_interval);
        } else {
            if (endpoint_was_reachable) {
                ESP_LOGW(TAG, "Internet unavailable (network IP lost)");
                endpoint_was_reachable = false;
            }
            vTaskDelay(pdMS_TO_TICKS(OTA_NO_NETWORK_RETRY_MS));
        }
    }
}

esp_err_t ota_manager_init(
    network_manager_handle_t network,
    const ota_manager_config_t *config)
{
    if (network == NULL || config == NULL ||
        config->manifest_url == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_network != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t url_length = strlen(config->manifest_url);
    size_t token_length = config->auth_token != NULL
        ? strlen(config->auth_token) : 0;
    if (url_length == 0 || url_length > OTA_MANIFEST_URL_MAX_LEN) {
        ESP_LOGE(TAG, "Manifest URL must contain 1-%u characters",
                 OTA_MANIFEST_URL_MAX_LEN);
        return ESP_ERR_INVALID_ARG;
    }
    if (token_length > 0 &&
        (token_length < 16 || token_length > OTA_TOKEN_MAX_LEN)) {
        ESP_LOGE(TAG, "OTA token must be empty or contain 16-127 characters");
        return ESP_ERR_INVALID_ARG;
    }
    if (config->check_interval_sec < 10 ||
        config->check_interval_sec > 3600) {
        ESP_LOGE(TAG, "OTA check interval must be between 10 and 3600 seconds");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(s_manifest_url, config->manifest_url, url_length + 1);
    if (token_length > 0) {
        memcpy(s_auth_token, config->auth_token, token_length + 1);
    } else {
        s_auth_token[0] = '\0';
    }
    s_check_interval_sec = config->check_interval_sec;
    s_network = network;

    ESP_LOGI(TAG, "OTA Manager initialized (interval=%lu sec)",
             (unsigned long)s_check_interval_sec);
    return ESP_OK;
}

esp_err_t ota_manager_start(void)
{
    if (s_network == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_task != NULL) {
        return ESP_OK;
    }

    /*
     * ota_manager_start() is called by the application after its critical
     * startup steps have succeeded. Keep rollback confirmation inside OTA.
     */
    ota_confirm_running_firmware_if_pending();

    if (xTaskCreate(
            ota_manager_task,
            "ota_manager",
            OTA_TASK_STACK_SIZE,
            NULL,
            OTA_TASK_PRIORITY,
            &s_task) != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
