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
#include "audio_manager.h"
#include "app.h"

static const char *TAG = "MAIN";
static wifi_ap_record_t s_ap_records[NETWORK_MANAGER_MAX_SCAN_RESULTS];

static void on_audio_event(
    audio_manager_event_t event,
    esp_err_t error,
    const char *path,
    void *user_ctx)
{
    (void)user_ctx;

    switch (event) {
    case AUDIO_MANAGER_EVENT_STARTED:
        ESP_LOGI(TAG, "Audio started: %s", path);
        break;

    case AUDIO_MANAGER_EVENT_PAUSED:
        ESP_LOGI(TAG, "Audio paused");
        break;

    case AUDIO_MANAGER_EVENT_RESUMED:
        ESP_LOGI(TAG, "Audio resumed");
        break;

    case AUDIO_MANAGER_EVENT_STOPPED:
        ESP_LOGI(TAG, "Audio stopped");
        break;

    case AUDIO_MANAGER_EVENT_FINISHED:
        ESP_LOGI(TAG, "Audio playback finished: %s", path);
        break;

    case AUDIO_MANAGER_EVENT_ERROR:
        ESP_LOGE(TAG, "Audio playback error: %s (%s)",
                 path, esp_err_to_name(error));
        break;

    default:
        break;
    }
}

/*
 * Called when Network Manager Internet connectivity changes.
 */
static void on_network_internet_changed(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    network_manager_handle_t network = (network_manager_handle_t)arg;
    network_status_t status = {0};
    (void)event_base;
    (void)event_data;

    if (network_manager_get_status(network, &status) != ESP_OK) {
        return;
    }

    if (event_id == NETWORK_EVENT_INTERNET_CONNECTED) {
        ESP_LOGI(TAG, "Internet is available");
    } else if (event_id == NETWORK_EVENT_INTERNET_DISCONNECTED) {
        ESP_LOGW(TAG, "Internet is unavailable (Wi-Fi: %s, IP: %s)",
                 status.wifi_connected ? "connected" : "disconnected",
                 status.has_ip ? status.ip_address : "none");
    }
}

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
    esp_err_t err;
    network_manager_handle_t network =
        (network_manager_handle_t)user_ctx;

    ESP_LOGW(TAG, "OTA: Application pre-flash callback");

    /* Remove App callbacks before the manager releases its handle. */
    err = esp_event_handler_unregister(
        NETWORK_MANAGER_EVENT,
        NETWORK_EVENT_SCAN_UPDATED,
        on_wifi_scan_updated
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi scan event unregister failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    err = esp_event_handler_unregister(
        NETWORK_MANAGER_EVENT,
        NETWORK_EVENT_INTERNET_CONNECTED,
        on_network_internet_changed
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Internet-connected event unregister failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    err = esp_event_handler_unregister(
        NETWORK_MANAGER_EVENT,
        NETWORK_EVENT_INTERNET_DISCONNECTED,
        on_network_internet_changed
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Internet-disconnected event unregister failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    /* Deinit performs STOP internally, then tears down Wi-Fi and ESP-Hosted. */
    err = network_manager_deinit(network);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Network Manager deinit failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Network Manager and ESP-Hosted deinitialized");

    /* Deinitialize LVGL before touching board peripherals */
    err = app_lvgl_deinit();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LVGL deinit failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    err = audio_manager_deinit();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Audio Manager deinit failed: %s",
                esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Audio Manager deinitialized");

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


/*
 * Basic SD card filesystem test:
 * create -> write -> close -> reopen -> read -> compare -> delete.
 * O_EXCL prevents overwriting an existing file.
 */
static esp_err_t test_sdcard_filesystem(void)
{
    static const char *mount_path = "/sdcard";
    static const char *test_path = "/sdcard/.exfat_rw_test.tmp";
    static const char test_data[] =
        "ESP-IDF FatFs exFAT read/write test\n";
    char read_data[sizeof(test_data) + 1] = {0};
    struct stat st;
    size_t written = 0;
    size_t received = 0;
    const size_t data_size = sizeof(test_data) - 1;

    ESP_LOGI(TAG, "========== SD Card Filesystem Test ==========");

    if (stat(mount_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        ESP_LOGE(TAG, "SD card mount path is unavailable: %s (errno=%d: %s)",
                 mount_path, errno, strerror(errno));
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Mount path verified: %s", mount_path);

    int fd = open(test_path, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) {
        ESP_LOGE(TAG, "Create test file failed: %s (errno=%d: %s)",
                 test_path, errno, strerror(errno));
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Test file created");

    while (written < data_size) {
        ssize_t n = write(fd, test_data + written, data_size - written);
        if (n < 0) {
            ESP_LOGE(TAG, "Write test file failed (errno=%d: %s)",
                     errno, strerror(errno));
            close(fd);
            unlink(test_path);
            return ESP_FAIL;
        }
        if (n == 0) {
            ESP_LOGE(TAG, "Write test file returned zero bytes");
            close(fd);
            unlink(test_path);
            return ESP_FAIL;
        }
        written += (size_t)n;
    }

    if (fsync(fd) != 0) {
        ESP_LOGE(TAG, "Sync test file failed (errno=%d: %s)",
                 errno, strerror(errno));
        close(fd);
        unlink(test_path);
        return ESP_FAIL;
    }

    if (close(fd) != 0) {
        ESP_LOGE(TAG, "Close after write failed (errno=%d: %s)",
                 errno, strerror(errno));
        unlink(test_path);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Wrote and closed %u bytes", (unsigned)written);

    fd = open(test_path, O_RDONLY);
    if (fd < 0) {
        ESP_LOGE(TAG, "Reopen test file failed (errno=%d: %s)",
                 errno, strerror(errno));
        unlink(test_path);
        return ESP_FAIL;
    }

    while (received < sizeof(read_data)) {
        ssize_t n = read(fd, read_data + received,
                         sizeof(read_data) - received);
        if (n < 0) {
            ESP_LOGE(TAG, "Read test file failed (errno=%d: %s)",
                     errno, strerror(errno));
            close(fd);
            unlink(test_path);
            return ESP_FAIL;
        }
        if (n == 0) {
            break;
        }
        received += (size_t)n;
    }

    if (close(fd) != 0) {
        ESP_LOGE(TAG, "Close after read failed (errno=%d: %s)",
                 errno, strerror(errno));
        unlink(test_path);
        return ESP_FAIL;
    }

    if (received != data_size ||
        memcmp(read_data, test_data, data_size) != 0) {
        ESP_LOGE(TAG, "Read-back verification failed (expected=%u, actual=%u)",
                 (unsigned)data_size, (unsigned)received);
        unlink(test_path);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Read-back verified: %u bytes match",
             (unsigned)received);

    if (unlink(test_path) != 0) {
        ESP_LOGE(TAG, "Delete test file failed (errno=%d: %s)",
                 errno, strerror(errno));
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Test file deleted");

    ESP_LOGI(TAG, "SD card filesystem test PASSED");
    ESP_LOGI(TAG, "==============================================");
    return ESP_OK;
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

    /* Initialize Audio Manager using the Board Manager audio_dac device. */
    err = audio_manager_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Audio Manager init failed: %s",
                 esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Audio Manager initialized");
    }

    /* Verify SD card filesystem read/write operations. */
    err = test_sdcard_filesystem();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD card filesystem test FAILED");
    }

    err = audio_manager_register_event_callback(
        on_audio_event,
        NULL
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Register audio callback failed: %s",
                esp_err_to_name(err));
        return;
    }

    err = audio_manager_set_volume(70.0f);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Set volume failed: %s",
                esp_err_to_name(err));
        return;
    }

    err = audio_manager_play_file("/sdcard/test.mp3");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Play file failed: %s",
                esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "MP3 playback queued");

    /* Initialize application and UI */
    err = app_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Application initialization failed: %s",
                 esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Application started");

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

    err = esp_event_handler_register(
        NETWORK_MANAGER_EVENT,
        NETWORK_EVENT_INTERNET_CONNECTED,
        on_network_internet_changed,
        network
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Internet-connected event registration failed: %s",
                 esp_err_to_name(err));
        return;
    }

    err = esp_event_handler_register(
        NETWORK_MANAGER_EVENT,
        NETWORK_EVENT_INTERNET_DISCONNECTED,
        on_network_internet_changed,
        network
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Internet-disconnected event registration failed: %s",
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
}
