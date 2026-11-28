#include "ota_manager.h"
#include "ota_manager_private.h"

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
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "psa/crypto.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ota_manager";

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

static bool ota_copy_optional_json_string(cJSON *object, const char *key,
                                          char *destination, size_t capacity)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (item == NULL || cJSON_IsNull(item)) {
        destination[0] = '\0';
        return true;
    }
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }

    size_t length = strlen(item->valuestring);
    if (length >= capacity) {
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
        ota_copy_optional_json_string(root, "description",
                                      manifest->description,
                                      sizeof(manifest->description)) &&
        ota_copy_optional_json_string(root, "release_date",
                                      manifest->release_date,
                                      sizeof(manifest->release_date)) &&
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


static esp_err_t ota_http_open_get(
    ota_manager_handle_t handle,
    const char *url,
    esp_http_client_handle_t *out_client,
    int64_t *out_content_length,
    bool *out_server_reachable
)
{
    if (handle == NULL ||
        url == NULL ||
        out_client == NULL ||
        out_content_length == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_client = NULL;
    *out_content_length = 0;

    if (out_server_reachable != NULL) {
        *out_server_reachable = false;
    }

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

    esp_http_client_handle_t client = esp_http_client_init(&config);

    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = ESP_OK;

    if (handle->auth_token[0] != '\0') {
        err = esp_http_client_set_header(
            client,
            "X-OTA-Token",
            handle->auth_token
        );
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
            ESP_LOGE(TAG, "HTTP connection failed: %s",
                     esp_err_to_name(err));
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

        bool is_redirect =
            status == 301 || status == 302 || status == 303 ||
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



static esp_err_t ota_fetch_manifest(
    ota_manager_handle_t handle,
    ota_manifest_t *manifest,
    bool *out_server_reachable
)
{
    if (handle == NULL || manifest == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_http_client_handle_t client = NULL;
    int64_t content_length = 0;

    char manifest_url[OTA_MANIFEST_URL_MAX_LEN + 40];

    int url_length = snprintf(
        manifest_url,
        sizeof(manifest_url),
        "%s%cota_cache_bust=%lld",
        handle->manifest_url,
        strchr(handle->manifest_url, '?') != NULL ? '&' : '?',
        (long long)esp_timer_get_time()
    );

    if (url_length < 0 ||
        (size_t)url_length >= sizeof(manifest_url)) {
        ESP_LOGE(TAG, "Could not build cache-busted manifest URL");
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = ota_http_open_get(
        handle,
        manifest_url,
        &client,
        &content_length,
        out_server_reachable
    );

    if (err != ESP_OK) {
        return err;
    }

    if (content_length <= 0 ||
        content_length > OTA_MANIFEST_MAX_SIZE) {
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
            (int)((size_t)content_length - received_total)
        );

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


static esp_err_t ota_download_and_install(
    ota_manager_handle_t handle,
    const ota_manifest_t *manifest
)
{
    if (handle == NULL || manifest == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_t *update_partition =
        esp_ota_get_next_update_partition(NULL);

    if (update_partition == NULL ||
        manifest->size == 0 ||
        manifest->size > update_partition->size) {
        ESP_LOGE(TAG, "No suitable OTA partition for %u bytes",
                 (unsigned)manifest->size);
        return ESP_ERR_INVALID_SIZE;
    }

    /*
     * Keep the complete image in a dedicated PSRAM allocation.
     * No OTA Flash operation is started until download and SHA-256
     * verification succeed.
     */
    uint8_t *firmware_buffer = heap_caps_malloc(
        manifest->size,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    if (firmware_buffer == NULL) {
        ESP_LOGE(TAG, "Could not allocate %u bytes in PSRAM for OTA",
                 (unsigned)manifest->size);
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_handle_t client = NULL;
    int64_t content_length = 0;

    esp_err_t err = ota_http_open_get(
        handle,
        manifest->firmware_url,
        &client,
        &content_length,
        NULL
    );

    if (err != ESP_OK) {
        goto cleanup_buffer;
    }

    if (content_length <= 0 ||
        (uint64_t)content_length != (uint64_t)manifest->size) {
        ESP_LOGE(TAG, "Firmware length mismatch: manifest=%u HTTP=%lld",
                 (unsigned)manifest->size,
                 (long long)content_length);
        err = ESP_ERR_INVALID_SIZE;
        goto cleanup_http;
    }

    psa_hash_operation_t sha = PSA_HASH_OPERATION_INIT;

    psa_status_t psa_status = psa_crypto_init();

    if (psa_status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "PSA crypto initialization failed: %d",
                 (int)psa_status);
        err = ESP_FAIL;
        goto cleanup_http;
    }

    psa_status = psa_hash_setup(&sha, PSA_ALG_SHA_256);

    if (psa_status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "PSA SHA-256 setup failed: %d",
                 (int)psa_status);
        err = ESP_FAIL;
        goto cleanup_http;
    }

    size_t downloaded = 0;

    while (downloaded < manifest->size) {
        size_t remaining = manifest->size - downloaded;

        size_t requested = remaining < OTA_DOWNLOAD_CHUNK_SIZE
            ? remaining
            : OTA_DOWNLOAD_CHUNK_SIZE;

        int received = esp_http_client_read(
            client,
            (char *)firmware_buffer + downloaded,
            (int)requested
        );

        if (received <= 0) {
            ESP_LOGE(TAG, "Firmware download interrupted: %d",
                     received);
            psa_hash_abort(&sha);
            err = ESP_FAIL;
            goto cleanup_http;
        }

        psa_status = psa_hash_update(
            &sha,
            firmware_buffer + downloaded,
            (size_t)received
        );

        if (psa_status != PSA_SUCCESS) {
            ESP_LOGE(TAG, "PSA SHA-256 update failed: %d",
                     (int)psa_status);
            psa_hash_abort(&sha);
            err = ESP_FAIL;
            goto cleanup_http;
        }

        downloaded += (size_t)received;

        ESP_LOGI(TAG, "Firmware download: %u/%u bytes",
                 (unsigned)downloaded,
                 (unsigned)manifest->size);
    }

    uint8_t digest[32];
    size_t digest_length = 0;

    psa_status = psa_hash_finish(
        &sha,
        digest,
        sizeof(digest),
        &digest_length
    );

    if (psa_status != PSA_SUCCESS ||
        digest_length != sizeof(digest)) {
        ESP_LOGE(TAG, "PSA SHA-256 finish failed: %d",
                 (int)psa_status);
        err = ESP_FAIL;
        goto cleanup_http;
    }

    if (downloaded != manifest->size) {
        ESP_LOGE(TAG, "Downloaded size mismatch: expected=%u actual=%u",
                 (unsigned)manifest->size,
                 (unsigned)downloaded);
        err = ESP_ERR_INVALID_SIZE;
        goto cleanup_http;
    }

    if (!ota_sha256_matches(digest, manifest->sha256)) {
        ESP_LOGE(TAG, "Firmware SHA-256 does not match manifest");
        err = ESP_ERR_INVALID_CRC;
        goto cleanup_http;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    client = NULL;

    ESP_LOGI(TAG, "Firmware downloaded and SHA-256 verified");

    /*
     * Let the application prepare its resources only after the complete
     * image is verified, and before the first OTA Flash operation.
     */
    if (handle->before_update != NULL) {
        err = handle->before_update(handle->user_ctx);

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Application rejected OTA before Flash: %s",
                     esp_err_to_name(err));
            goto cleanup_buffer;
        }
    }

    esp_ota_handle_t ota_handle = 0;

    err = esp_ota_begin(
        update_partition,
        manifest->size,
        &ota_handle
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s",
                 esp_err_to_name(err));
        goto cleanup_buffer;
    }

    /*
     * Copy each PSRAM chunk into an internal task-stack buffer before
     * esp_ota_write(), so the Flash driver does not need to read its
     * source from PSRAM while Flash cache is unavailable.
     */
    uint8_t write_buffer[OTA_DOWNLOAD_CHUNK_SIZE];
    size_t written = 0;

    while (written < manifest->size) {
        size_t remaining = manifest->size - written;

        size_t chunk_size = remaining < sizeof(write_buffer)
            ? remaining
            : sizeof(write_buffer);

        memcpy(
            write_buffer,
            firmware_buffer + written,
            chunk_size
        );

        err = esp_ota_write(
            ota_handle,
            write_buffer,
            chunk_size
        );

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed at %u bytes: %s",
                     (unsigned)written,
                     esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            goto cleanup_buffer;
        }

        written += chunk_size;
    }

    err = esp_ota_end(ota_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Firmware image validation failed: %s",
                 esp_err_to_name(err));
        goto cleanup_buffer;
    }

    err = esp_ota_set_boot_partition(update_partition);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not select new boot partition: %s",
                 esp_err_to_name(err));
        goto cleanup_buffer;
    }

    ESP_LOGW(TAG, "Firmware %s validated; rebooting into %s",
             manifest->version,
             update_partition->label);

    heap_caps_free(firmware_buffer);

    vTaskDelay(pdMS_TO_TICKS(700));
    esp_restart();

    return ESP_OK;

cleanup_http:
    if (client != NULL) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }

cleanup_buffer:
    heap_caps_free(firmware_buffer);
    return err;
}


static void ota_clear_pending_update(ota_manager_handle_t handle)
{
    if (handle == NULL) {
        return;
    }

    if (xSemaphoreTake(handle->manifest_mutex, portMAX_DELAY) == pdTRUE) {
        handle->update_pending = false;
        handle->update_confirmed = false;
        handle->update_in_progress = false;

        memset(&handle->pending_manifest, 0,
               sizeof(handle->pending_manifest));

        xSemaphoreGive(handle->manifest_mutex);
    }
}

static bool ota_take_confirmed_update(
    ota_manager_handle_t handle,
    ota_manifest_t *manifest
)
{
    if (handle == NULL || manifest == NULL) {
        return false;
    }

    bool confirmed = false;

    if (xSemaphoreTake(handle->manifest_mutex, portMAX_DELAY) == pdTRUE) {
        if (handle->update_pending &&
            handle->update_confirmed &&
            !handle->update_in_progress) {

            handle->update_confirmed = false;
            handle->update_in_progress = true;

            *manifest = handle->pending_manifest;

            confirmed = true;
        }

        xSemaphoreGive(handle->manifest_mutex);
    }

    return confirmed;
}

static void ota_mark_manual_update_failed(ota_manager_handle_t handle)
{
    if (handle == NULL) {
        return;
    }

    if (xSemaphoreTake(handle->manifest_mutex, portMAX_DELAY) == pdTRUE) {
        handle->update_in_progress = false;
        handle->update_confirmed = false;

        xSemaphoreGive(handle->manifest_mutex);
    }
}


static void ota_check_for_update(
    ota_manager_handle_t handle,
    bool *out_server_reachable
)
{
    ota_manifest_t manifest = {0};
    const esp_app_desc_t *running_app = esp_app_get_description();

    if (out_server_reachable != NULL) {
        *out_server_reachable = false;
    }

    esp_err_t err = ota_fetch_manifest(
        handle,
        &manifest,
        out_server_reachable
    );

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not read firmware manifest: %s",
                 esp_err_to_name(err));
        return;
    }

    if (strcmp(manifest.project, "JC1060P470_I_W_Y") != 0) {
        ESP_LOGE(TAG, "Manifest is for another project: %s",
                 manifest.project);
        return;
    }

    ESP_LOGI(TAG, "Firmware version: running=%s available=%s",
             running_app->version, manifest.version);

    if (!ota_version_is_newer(manifest.version, running_app->version)) {
        ota_clear_pending_update(handle);

        ESP_LOGI(TAG, "Firmware is up to date");
        return;
    }

    if (handle->mode == OTA_MODE_AUTO) {
        ESP_LOGW(TAG, "New firmware found; downloading %s",
                 manifest.version);

        err = ota_download_and_install(handle, &manifest);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA update failed: %s",
                     esp_err_to_name(err));
        }

        return;
    }

    bool notify_application = false;

    if (xSemaphoreTake(handle->manifest_mutex, portMAX_DELAY) == pdTRUE) {

        if (!handle->update_confirmed &&
            !handle->update_in_progress) {

            notify_application =
                !handle->update_pending ||
                strcmp(
                    handle->pending_manifest.version,
                    manifest.version
                ) != 0;

            if (notify_application) {
                handle->pending_manifest = manifest;
                handle->update_pending = true;
            }
        }

        xSemaphoreGive(handle->manifest_mutex);
    }

    if (notify_application) {
        ESP_LOGW(TAG,
                 "New firmware available; waiting for application confirmation: %s",
                 manifest.version);

        if (handle->update_available != NULL) {
            ota_firmware_info_t info = {
                .size = manifest.size,
            };

            memcpy(info.version, manifest.version,
                   sizeof(info.version));

            memcpy(info.description, manifest.description,
                   sizeof(info.description));

            memcpy(info.release_date, manifest.release_date,
                   sizeof(info.release_date));

            handle->update_available(&info, handle->user_ctx);
        }
    }
}

/*
 * Confirm the running image only after the application has completed its
 * startup sequence. If it is still pending verification, a reboot before
 * this point will allow the bootloader to roll back to the previous image.
 */
void ota_manager_confirm_running_firmware(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (running == NULL) {
        ESP_LOGE(TAG, "Could not get running OTA partition");
        return;
    }

    esp_ota_img_states_t state;
    esp_err_t err = esp_ota_get_state_partition(running, &state);

    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "Running firmware has no OTA state");
        return;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not read OTA image state: %s",
                 esp_err_to_name(err));
        return;
    }

    switch (state) {
    case ESP_OTA_IMG_PENDING_VERIFY:
        err = esp_ota_mark_app_valid_cancel_rollback();

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Could not confirm running firmware: %s",
                     esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Running firmware confirmed; rollback cancelled");
        }
        break;

    case ESP_OTA_IMG_VALID:
        ESP_LOGD(TAG, "Running firmware already confirmed");
        break;

    default:
        ESP_LOGW(TAG, "Running firmware has unexpected OTA state: %d",
                 (int)state);
        break;
    }
}

static void ota_manager_task(void *arg)
{
    ota_manager_context_t *ctx = (ota_manager_context_t *)arg;

    const TickType_t check_interval =
        pdMS_TO_TICKS(ctx->check_interval_sec * 1000U);
    bool endpoint_was_reachable = false;

    ESP_LOGI(TAG, "OTA task started; manifest: %s",
             ctx->manifest_url);

    for (;;) {
        network_status_t status = {0};
        esp_err_t err = network_manager_get_status(
            ctx->network, 
            &status
        );

        if (err == ESP_OK && status.has_ip) {
            bool endpoint_reachable = false;
            bool manifest_checked = false;

            if (ctx->mode == OTA_MODE_MANUAL) {
                ota_manifest_t confirmed_manifest = {0};
                if (ota_take_confirmed_update(ctx, &confirmed_manifest)) {
                    ESP_LOGW(TAG, "Application confirmed firmware %s; starting OTA",
                             confirmed_manifest.version);
                    err = ota_download_and_install(
                        ctx,
                        &confirmed_manifest
                    );
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "Confirmed OTA update failed: %s",
                                 esp_err_to_name(err));
                        ota_mark_manual_update_failed(
                            ctx
                        );
                    }
                } else {
                    ota_check_for_update(
                        ctx,
                        &endpoint_reachable
                    );
                    manifest_checked = true;
                }
            } else {
                ota_check_for_update(
                    ctx,
                    &endpoint_reachable
                );
                manifest_checked = true;
            }

            if (manifest_checked) {
                if (endpoint_reachable && !endpoint_was_reachable) {
                    ESP_LOGI(TAG, "OTA manifest endpoint reachable");
                } else if (!endpoint_reachable && endpoint_was_reachable) {
                    ESP_LOGW(TAG, "OTA manifest endpoint unreachable");
                }
                endpoint_was_reachable = endpoint_reachable;
            }
            (void)ulTaskNotifyTake(pdTRUE, check_interval);
        } else {
            if (endpoint_was_reachable) {
                ESP_LOGW(TAG, "Internet unavailable (network IP lost)");
                endpoint_was_reachable = false;
            }
            (void)ulTaskNotifyTake(
                pdTRUE, pdMS_TO_TICKS(OTA_NO_NETWORK_RETRY_MS));
        }
    }
}

esp_err_t ota_manager_init(
    ota_manager_handle_t *out_handle,
    const ota_manager_config_t *config)
{
    if (out_handle == NULL||
        config == NULL||
        config->network == NULL||
        config->manifest_url == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_handle = NULL;

    ota_manager_context_t *ctx = heap_caps_calloc(
        1,
        sizeof(ota_manager_context_t),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
    );
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t url_length = strlen(config->manifest_url);
    size_t token_length = config->auth_token != NULL
        ? strlen(config->auth_token) : 0;
    if (url_length == 0 || url_length > OTA_MANIFEST_URL_MAX_LEN) {
        ESP_LOGE(TAG, "Manifest URL must contain 1-%u characters",
                 OTA_MANIFEST_URL_MAX_LEN);
        heap_caps_free(ctx);
        return ESP_ERR_INVALID_ARG;
    }
    if (token_length > 0 &&
        (token_length < 16 || token_length > OTA_TOKEN_MAX_LEN)) {
        ESP_LOGE(TAG, "OTA token must be empty or contain 16-127 characters");
        heap_caps_free(ctx);
        return ESP_ERR_INVALID_ARG;
    }
    if (config->mode != OTA_MODE_AUTO &&
        config->mode != OTA_MODE_MANUAL) {
        ESP_LOGE(TAG, "Invalid OTA mode");
        heap_caps_free(ctx);
        return ESP_ERR_INVALID_ARG;
    }
    if (config->check_interval_sec < 10 ||
        config->check_interval_sec > 3600) {
        ESP_LOGE(TAG, "OTA check interval must be between 10 and 3600 seconds");
        heap_caps_free(ctx);
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(ctx->manifest_url, config->manifest_url, url_length + 1);
    if (token_length > 0) {
        memcpy(ctx->auth_token, config->auth_token, token_length + 1);
    } else {
        ctx->auth_token[0] = '\0';
    }
    ctx->check_interval_sec = config->check_interval_sec;
    ctx->mode = config->mode;
    ctx->update_available = config->update_available;
    ctx->before_update = config->before_update;
    ctx->user_ctx = config->user_ctx;
    ctx->network = config->network;

    ctx->manifest_mutex = xSemaphoreCreateMutexStatic(
        &ctx->manifest_mutex_buffer
    );
    if (ctx->manifest_mutex == NULL) {
        heap_caps_free(ctx);
        return ESP_ERR_NO_MEM;
    }

    *out_handle = ctx;

    ctx->task = xTaskCreateStaticPinnedToCore(
        ota_manager_task,
        "ota_manager_task",
        OTA_TASK_STACK_DEPTH,
        ctx,
        OTA_TASK_PRIORITY,
        ctx->task_stack,
        &ctx->task_buffer,
        OTA_TASK_CORE_ID
    );
    if (ctx->task == NULL) {
        *out_handle = NULL;
        vSemaphoreDelete(ctx->manifest_mutex);
        heap_caps_free(ctx);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "OTA Manager initialized (mode=%s interval=%lu sec)",
             ctx->mode == OTA_MODE_AUTO ? "AUTO" : "MANUAL",
             (unsigned long)ctx->check_interval_sec);

    return ESP_OK;
}

esp_err_t ota_manager_get_status(
    ota_manager_handle_t handle,
    ota_manager_status_t *status)
{
    if (handle == NULL || status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(status, 0, sizeof(*status));

    const esp_app_desc_t *running_app = esp_app_get_description();
    if (running_app == NULL) {
        return ESP_FAIL;
    }

    snprintf(status->current_version,
             sizeof(status->current_version),
             "%s",
             running_app->version);

    if (xSemaphoreTake(handle->manifest_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }

    if (handle->update_pending) {
        status->update_available = true;

        memcpy(status->available_firmware.version,
               handle->pending_manifest.version,
               sizeof(status->available_firmware.version));

        memcpy(status->available_firmware.description,
               handle->pending_manifest.description,
               sizeof(status->available_firmware.description));

        memcpy(status->available_firmware.release_date,
               handle->pending_manifest.release_date,
               sizeof(status->available_firmware.release_date));

        status->available_firmware.size =
            handle->pending_manifest.size;
    }

    xSemaphoreGive(handle->manifest_mutex);

    return ESP_OK;
}

esp_err_t ota_manager_confirm_update(ota_manager_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    bool notify_task = false;
    TaskHandle_t task = NULL;

    if (xSemaphoreTake(handle->manifest_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }

    if (handle->mode == OTA_MODE_MANUAL &&
        handle->update_pending &&
        !handle->update_confirmed &&
        !handle->update_in_progress &&
        handle->task != NULL) {

        handle->update_confirmed = true;
        notify_task = true;
        task = handle->task;
    }

    xSemaphoreGive(handle->manifest_mutex);

    if (notify_task) {
        ESP_LOGI(TAG, "Application confirmed pending firmware update");
        xTaskNotifyGive(task);
    } else {
        ESP_LOGI(TAG, "No firmware update is waiting for confirmation");
    }

    return ESP_OK;
}