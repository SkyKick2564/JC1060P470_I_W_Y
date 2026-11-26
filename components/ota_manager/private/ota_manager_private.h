#ifndef OTA_MANAGER_PRIVATE_H
#define OTA_MANAGER_PRIVATE_H

#include "ota_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define OTA_TASK_STACK_SIZE       16384
#define OTA_TASK_STACK_DEPTH  \
    (OTA_TASK_STACK_SIZE / sizeof(StackType_t))

#define OTA_TASK_PRIORITY         4
#define OTA_NO_NETWORK_RETRY_MS   5000
#define OTA_HTTP_TIMEOUT_MS       15000
#define OTA_MANIFEST_MAX_SIZE     4096
#define OTA_DOWNLOAD_CHUNK_SIZE   4096
#define OTA_TOKEN_MAX_LEN         127
#define OTA_MANIFEST_URL_MAX_LEN  512
#define OTA_SHA256_HEX_LEN        64

typedef struct {
    char project[48];
    char version[32];
    char description[256];
    char release_date[32];
    char firmware_url[512];
    char sha256[OTA_SHA256_HEX_LEN + 1];
    size_t size;
} ota_manifest_t;

typedef struct ota_manager_context {
    network_manager_handle_t network;

    /* FreeRTOS Task */
    StaticTask_t task_buffer;
    StackType_t task_stack[OTA_TASK_STACK_DEPTH];
    TaskHandle_t task;

    char manifest_url[OTA_MANIFEST_URL_MAX_LEN + 1];
    char auth_token[OTA_TOKEN_MAX_LEN + 1];

    uint32_t check_interval_sec;

    ota_before_update_cb_t before_update;
    ota_update_available_cb_t update_available; 
    void *user_ctx;

    ota_mode_t mode;

    /* Manifest Mutex */
    StaticSemaphore_t manifest_mutex_buffer;
    SemaphoreHandle_t manifest_mutex;

    /* Task Exit Semaphore */
    StaticSemaphore_t task_exit_sem_buffer;
    SemaphoreHandle_t task_exit_sem;

    ota_manifest_t pending_manifest;
    bool update_pending;
    bool update_confirmed;
    bool update_in_progress;

}ota_manager_context_t;

#endif // OTA_MANAGER_PRIVATE_H