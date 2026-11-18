#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_board_manager.h"
#include "esp_board_manager_includes.h"
#include "esp_codec_dev.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

#include "audio_manager.h"

#define AUDIO_MANAGER_TAG "AUDIO_MANAGER"
#define AUDIO_MANAGER_TASK_STACK_SIZE (20 * 1024)
#define AUDIO_MANAGER_TASK_PRIORITY 5
#define AUDIO_MANAGER_INPUT_BUFFER_SIZE 2048
#define AUDIO_MANAGER_OUTPUT_BUFFER_SIZE 8192
#define AUDIO_MANAGER_DEFAULT_VOLUME 50.0f

#define AUDIO_COMMAND_PLAY   (1UL << 0)
#define AUDIO_COMMAND_PAUSE  (1UL << 1)
#define AUDIO_COMMAND_RESUME (1UL << 2)
#define AUDIO_COMMAND_STOP   (1UL << 3)
#define AUDIO_COMMAND_EXIT   (1UL << 4)

static dev_audio_codec_handles_t *s_audio_dac;
static bool s_codec_opened;
static void audio_manager_player_task(void *arg);
static TaskHandle_t s_player_task;
static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_task_exit_sem;
static audio_manager_state_t s_state = AUDIO_MANAGER_STATE_UNINITIALIZED;
static audio_manager_event_callback_t s_event_callback;
static void *s_event_user_ctx;
static float s_volume = AUDIO_MANAGER_DEFAULT_VOLUME;
static char s_current_path[AUDIO_MANAGER_PATH_MAX];
static bool s_current_is_url;

#define AUDIO_MANAGER_PCM_QUEUE_LENGTH 8
#define AUDIO_MANAGER_OUTPUT_TASK_STACK_SIZE (6 * 1024)
#define AUDIO_MANAGER_OUTPUT_TASK_PRIORITY (AUDIO_MANAGER_TASK_PRIORITY + 2)

typedef struct {
    size_t size;
    uint8_t data[];
} audio_pcm_block_t;

typedef struct {
    QueueHandle_t queue;
    SemaphoreHandle_t done;
    volatile esp_err_t error;
    esp_codec_dev_handle_t codec;
} audio_output_context_t;

static void audio_manager_output_task(void *arg)
{
    audio_output_context_t *ctx = (audio_output_context_t *)arg;
    audio_pcm_block_t *block = NULL;

    while (xQueueReceive(ctx->queue, &block, portMAX_DELAY) == pdTRUE) {
        if (block == NULL) {
            break;
        }

        if (ctx->error == ESP_OK) {
            esp_err_t err = esp_codec_dev_write(ctx->codec, block->data,
                                                 (int)block->size);
            if (err != ESP_CODEC_DEV_OK) {
                ctx->error = err;
            }
        }
        free(block);
    }

    xSemaphoreGive(ctx->done);
    vTaskDelete(NULL);
}

static void audio_manager_set_state(audio_manager_state_t state)
{
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_state = state;
        xSemaphoreGive(s_lock);
    } else {
        s_state = state;
    }
}

static void audio_manager_emit_event(
    audio_manager_event_t event,
    esp_err_t error)
{
    audio_manager_event_callback_t callback;
    void *user_ctx;
    const char *path = s_current_path;

    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        callback = s_event_callback;
        user_ctx = s_event_user_ctx;
        xSemaphoreGive(s_lock);
    } else {
        callback = s_event_callback;
        user_ctx = s_event_user_ctx;
    }

    if (callback != NULL) {
        callback(event, error, path, user_ctx);
    }
}

static esp_audio_simple_dec_type_t audio_manager_get_decoder_type(
    const char *path)
{
    const char *extension = strrchr(path, '.');

    if (extension == NULL) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
    }

    extension++;
    size_t extension_length = strcspn(extension, "?#");
    if (extension_length == 3 && strncasecmp(extension, "mp3", 3) == 0) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    }
    if (extension_length == 3 && strncasecmp(extension, "wav", 3) == 0) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_WAV;
    }

    return ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
}


static bool audio_manager_contains_case_insensitive(const char *text,
                                                     const char *value)
{
    size_t value_length;

    if (text == NULL || value == NULL) {
        return false;
    }
    value_length = strlen(value);
    while (*text != '\0') {
        if (strncasecmp(text, value, value_length) == 0) {
            return true;
        }
        text++;
    }
    return false;
}

static bool audio_manager_is_http_url(const char *value)
{
    return value != NULL &&
           (strncasecmp(value, "http://", 7) == 0 ||
            strncasecmp(value, "https://", 8) == 0);
}

static bool audio_manager_is_playlist_url(const char *url,
                                          const char *content_type)
{
    const char *query = strpbrk(url, "?#");
    size_t length = query != NULL ? (size_t)(query - url) : strlen(url);

    if (audio_manager_contains_case_insensitive(content_type, "audio/x-scpls") ||
        audio_manager_contains_case_insensitive(content_type, "application/pls") ||
        audio_manager_contains_case_insensitive(content_type, "audio/x-mpegurl") ||
        audio_manager_contains_case_insensitive(content_type, "application/x-mpegurl")) {
        return true;
    }

    return (length >= 4 && strncasecmp(url + length - 4, ".pls", 4) == 0) ||
           (length >= 4 && strncasecmp(url + length - 4, ".m3u", 4) == 0);
}

static esp_err_t audio_manager_parse_playlist(char *playlist,
                                               char *stream_url,
                                               size_t stream_url_size)
{
    char *line = playlist;

    while (line != NULL && *line != '\0') {
        char *next = strpbrk(line, "\r\n");
        if (next != NULL) {
            *next++ = '\0';
            while (*next == '\r' || *next == '\n') {
                next++;
            }
        }

        while (*line == ' ' || *line == '\t') {
            line++;
        }
        char *end = line + strlen(line);
        while (end > line && (end[-1] == ' ' || end[-1] == '\t')) {
            *--end = '\0';
        }

        if (strncasecmp(line, "File", 4) == 0) {
            char *equal = strchr(line, '=');
            if (equal != NULL) {
                line = equal + 1;
            }
        }

        if (audio_manager_is_http_url(line)) {
            size_t length = strlen(line);
            if (length >= stream_url_size) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(stream_url, line, length + 1);
            return ESP_OK;
        }
        line = next;
    }

    return ESP_ERR_NOT_FOUND;
}

static esp_err_t audio_manager_http_event_handler(esp_http_client_event_t *event)
{
    char *content_type = (char *)event->user_data;

    if (event->event_id == HTTP_EVENT_ON_HEADER &&
        event->header_key != NULL && event->header_value != NULL &&
        strcasecmp(event->header_key, "Content-Type") == 0 &&
        content_type != NULL) {
        snprintf(content_type, 96, "%s", event->header_value);
    }
    return ESP_OK;
}

static esp_err_t audio_manager_http_open(const char *url,
                                         esp_http_client_handle_t *client,
                                         char *content_type)
{
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 15000,
        .buffer_size = 4096,
        .buffer_size_tx = 1024,
        .user_agent = "ESP32-AudioManager/1.0",
        .keep_alive_enable = true,
        .event_handler = audio_manager_http_event_handler,
        .user_data = content_type,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    content_type[0] = '\0';
    *client = esp_http_client_init(&config);
    if (*client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_open(*client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(*client);
        *client = NULL;
        return err;
    }

    int64_t content_length = esp_http_client_fetch_headers(*client);
    if (content_length < 0) {
        esp_http_client_close(*client);
        esp_http_client_cleanup(*client);
        *client = NULL;
        return ESP_FAIL;
    }

    int status = esp_http_client_get_status_code(*client);
    if (status < 200 || status >= 300) {
        ESP_LOGE(AUDIO_MANAGER_TAG, "HTTP request failed, status=%d", status);
        esp_http_client_close(*client);
        esp_http_client_cleanup(*client);
        *client = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(AUDIO_MANAGER_TAG, "HTTP stream opened: status=%d, type=%s",
             status, content_type[0] != '\0' ? content_type : "unknown");
    return ESP_OK;
}

static esp_audio_simple_dec_type_t audio_manager_get_stream_decoder_type(
    const char *url,
    const char *content_type)
{
    if (audio_manager_contains_case_insensitive(content_type, "audio/mpeg") ||
        audio_manager_contains_case_insensitive(content_type, "audio/mp3") ||
        audio_manager_contains_case_insensitive(content_type, "audio/x-mpeg")) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    }

    return audio_manager_get_decoder_type(url);
}

bool audio_manager_is_initialized(void)
{
    return s_player_task != NULL && s_state != AUDIO_MANAGER_STATE_UNINITIALIZED;
}

esp_err_t audio_manager_init(void)
{
    esp_err_t err;

    if (audio_manager_is_initialized()) {
        return ESP_OK;
    }

    if (!esp_board_manager_check_name("audio_dac")) {
        ESP_LOGE(AUDIO_MANAGER_TAG, "Board does not provide audio_dac");
        return ESP_ERR_NOT_SUPPORTED;
    }

    err = esp_board_manager_get_device_handle(
        "audio_dac",
        (void **)&s_audio_dac
    );
    if (err != ESP_OK) {
        s_audio_dac = NULL;
        ESP_LOGE(AUDIO_MANAGER_TAG, "Get audio_dac handle failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    if (s_audio_dac == NULL || s_audio_dac->codec_dev == NULL) {
        s_audio_dac = NULL;
        return ESP_ERR_INVALID_STATE;
    }

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        s_audio_dac = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_task_exit_sem = xSemaphoreCreateBinary();
    if (s_task_exit_sem == NULL) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        s_audio_dac = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_state = AUDIO_MANAGER_STATE_IDLE;
    if (xTaskCreate(
            audio_manager_player_task,
            "audio_player",
            AUDIO_MANAGER_TASK_STACK_SIZE,
            NULL,
            AUDIO_MANAGER_TASK_PRIORITY,
            &s_player_task) != pdPASS) {
        vSemaphoreDelete(s_task_exit_sem);
        s_task_exit_sem = NULL;
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        s_audio_dac = NULL;
        s_state = AUDIO_MANAGER_STATE_UNINITIALIZED;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(AUDIO_MANAGER_TAG, "Initialized; WAV and MP3 playback available");
    return ESP_OK;
}

esp_err_t audio_manager_deinit(void)
{
    audio_manager_state_t state;
    TickType_t start_tick;

    if (!audio_manager_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Do not allow deinit from the player task's event callback. */
    if (xTaskGetCurrentTaskHandle() == s_player_task) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    state = s_state;
    if (state == AUDIO_MANAGER_STATE_QUEUED ||
        state == AUDIO_MANAGER_STATE_PLAYING ||
        state == AUDIO_MANAGER_STATE_PAUSED ||
        state == AUDIO_MANAGER_STATE_STOPPING) {
        s_state = AUDIO_MANAGER_STATE_STOPPING;
    }
    xSemaphoreGive(s_lock);

    if (state == AUDIO_MANAGER_STATE_QUEUED ||
        state == AUDIO_MANAGER_STATE_PLAYING ||
        state == AUDIO_MANAGER_STATE_PAUSED ||
        state == AUDIO_MANAGER_STATE_STOPPING) {
        xTaskNotify(s_player_task, AUDIO_COMMAND_STOP, eSetBits);

        start_tick = xTaskGetTickCount();
        while (true) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            state = s_state;
            xSemaphoreGive(s_lock);

            if (state != AUDIO_MANAGER_STATE_STOPPING &&
                state != AUDIO_MANAGER_STATE_QUEUED &&
                state != AUDIO_MANAGER_STATE_PLAYING &&
                state != AUDIO_MANAGER_STATE_PAUSED) {
                break;
            }

            if ((xTaskGetTickCount() - start_tick) >= pdMS_TO_TICKS(10000)) {
                ESP_LOGE(AUDIO_MANAGER_TAG,
                         "Timed out waiting for playback to stop");
                return ESP_ERR_TIMEOUT;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    /* Ask the idle player task to exit, then wait for its acknowledgement. */
    xTaskNotify(s_player_task, AUDIO_COMMAND_EXIT, eSetBits);
    if (xSemaphoreTake(s_task_exit_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(AUDIO_MANAGER_TAG, "Timed out waiting for player task exit");
        return ESP_ERR_TIMEOUT;
    }

    s_player_task = NULL;
    vSemaphoreDelete(s_task_exit_sem);
    s_task_exit_sem = NULL;
    vSemaphoreDelete(s_lock);
    s_lock = NULL;
    s_audio_dac = NULL;
    s_event_callback = NULL;
    s_event_user_ctx = NULL;
    s_current_path[0] = '\0';
    s_current_is_url = false;
    s_state = AUDIO_MANAGER_STATE_UNINITIALIZED;
    return ESP_OK;
}

esp_err_t audio_manager_register_event_callback(
    audio_manager_event_callback_t callback,
    void *user_ctx)
{
    if (!audio_manager_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_event_callback = callback;
    s_event_user_ctx = user_ctx;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t audio_manager_play_file(const char *path)
{
    size_t path_length;

    if (!audio_manager_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (path == NULL || path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    path_length = strlen(path);
    if (path_length >= sizeof(s_current_path)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (audio_manager_get_decoder_type(path) == ESP_AUDIO_SIMPLE_DEC_TYPE_NONE) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_state == AUDIO_MANAGER_STATE_QUEUED ||
        s_state == AUDIO_MANAGER_STATE_PLAYING ||
        s_state == AUDIO_MANAGER_STATE_PAUSED ||
        s_state == AUDIO_MANAGER_STATE_STOPPING) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(s_current_path, path, path_length + 1);
    s_current_is_url = false;
    s_state = AUDIO_MANAGER_STATE_QUEUED;
    xSemaphoreGive(s_lock);

    xTaskNotify(s_player_task, AUDIO_COMMAND_PLAY, eSetBits);
    return ESP_OK;
}


esp_err_t audio_manager_play_url(const char *url)
{
    size_t url_length;

    if (!audio_manager_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!audio_manager_is_http_url(url)) {
        return ESP_ERR_INVALID_ARG;
    }

    url_length = strlen(url);
    if (url_length >= sizeof(s_current_path)) {
        return ESP_ERR_INVALID_SIZE;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_state == AUDIO_MANAGER_STATE_QUEUED ||
        s_state == AUDIO_MANAGER_STATE_PLAYING ||
        s_state == AUDIO_MANAGER_STATE_PAUSED ||
        s_state == AUDIO_MANAGER_STATE_STOPPING) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(s_current_path, url, url_length + 1);
    s_current_is_url = true;
    s_state = AUDIO_MANAGER_STATE_QUEUED;
    xSemaphoreGive(s_lock);

    xTaskNotify(s_player_task, AUDIO_COMMAND_PLAY, eSetBits);
    return ESP_OK;
}

esp_err_t audio_manager_pause(void)
{
    if (!audio_manager_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_state != AUDIO_MANAGER_STATE_PLAYING) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreGive(s_lock);

    xTaskNotify(s_player_task, AUDIO_COMMAND_PAUSE, eSetBits);
    return ESP_OK;
}

esp_err_t audio_manager_resume(void)
{
    if (!audio_manager_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_state != AUDIO_MANAGER_STATE_PAUSED) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreGive(s_lock);

    xTaskNotify(s_player_task, AUDIO_COMMAND_RESUME, eSetBits);
    return ESP_OK;
}

esp_err_t audio_manager_stop(void)
{
    if (!audio_manager_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_state != AUDIO_MANAGER_STATE_QUEUED &&
        s_state != AUDIO_MANAGER_STATE_PLAYING &&
        s_state != AUDIO_MANAGER_STATE_PAUSED) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_state = AUDIO_MANAGER_STATE_STOPPING;
    xSemaphoreGive(s_lock);

    xTaskNotify(s_player_task, AUDIO_COMMAND_STOP, eSetBits);
    return ESP_OK;
}

esp_err_t audio_manager_set_volume(float volume)
{
    esp_err_t err = ESP_OK;

    if (!audio_manager_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (volume < 0.0f || volume > 100.0f) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_volume = volume;
    if (s_codec_opened) {
        err = esp_codec_dev_set_out_vol(s_audio_dac->codec_dev, volume);
    }
    xSemaphoreGive(s_lock);

    if (err != ESP_CODEC_DEV_OK) {
        ESP_LOGE(AUDIO_MANAGER_TAG, "Set output volume failed: %s",
                 esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

esp_err_t audio_manager_get_volume(float *volume)
{
    if (!audio_manager_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (volume == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    *volume = s_volume;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t audio_manager_get_state(audio_manager_state_t *state)
{
    if (!audio_manager_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    *state = s_state;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t audio_manager_get_current_file(char *path, size_t path_size)
{
    size_t required;

    if (!audio_manager_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (path == NULL || path_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    required = strlen(s_current_path) + 1;
    if (path_size < required) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(path, s_current_path, required);
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

static esp_err_t audio_manager_open_codec(
    const esp_audio_simple_dec_info_t *info)
{
    esp_codec_dev_sample_info_t sample_info = {
        .sample_rate = info->sample_rate,
        .channel = info->channel,
        .bits_per_sample = info->bits_per_sample,
    };

    esp_err_t err = esp_codec_dev_open(s_audio_dac->codec_dev, &sample_info);
    if (err != ESP_CODEC_DEV_OK) {
        ESP_LOGE(AUDIO_MANAGER_TAG, "Open audio codec failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    err = esp_codec_dev_set_out_vol(s_audio_dac->codec_dev, s_volume);
    if (err != ESP_CODEC_DEV_OK) {
        esp_codec_dev_close(s_audio_dac->codec_dev);
        return err;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_codec_opened = true;
    xSemaphoreGive(s_lock);

    ESP_LOGI(AUDIO_MANAGER_TAG,
             "Codec opened: sample_rate=%" PRIu32 ", channels=%u, bits=%u",
             info->sample_rate, (unsigned)info->channel,
             (unsigned)info->bits_per_sample);
    return ESP_OK;
}

/*
 * Waits while paused and observes stop requests between decoder/write calls.
 * The player task owns decoder and codec resources for the whole operation.
 */
static bool audio_manager_process_control(void)
{
    uint32_t commands = 0;

    xTaskNotifyWait(0, UINT32_MAX, &commands, 0);

    if ((commands & AUDIO_COMMAND_STOP) != 0) {
        return false;
    }

    if ((commands & AUDIO_COMMAND_PAUSE) != 0) {
        audio_manager_set_state(AUDIO_MANAGER_STATE_PAUSED);
        audio_manager_emit_event(AUDIO_MANAGER_EVENT_PAUSED, ESP_OK);

        while (true) {
            commands = 0;
            xTaskNotifyWait(0, UINT32_MAX, &commands, portMAX_DELAY);
            if ((commands & AUDIO_COMMAND_STOP) != 0) {
                return false;
            }
            if ((commands & AUDIO_COMMAND_RESUME) != 0) {
                audio_manager_set_state(AUDIO_MANAGER_STATE_PLAYING);
                audio_manager_emit_event(AUDIO_MANAGER_EVENT_RESUMED, ESP_OK);
                break;
            }
        }
    }

    return true;
}

static esp_err_t audio_manager_play_file_internal(
    const char *path,
    esp_audio_simple_dec_type_t decoder_type,
    bool is_url,
    bool *stopped)
{
    FILE *file = NULL;
    esp_http_client_handle_t http_client = NULL;
    char content_type[96] = {0};
    char resolved_url[AUDIO_MANAGER_PATH_MAX] = {0};
    uint8_t *input_buffer = NULL;
    uint8_t *output_buffer = NULL;
    size_t output_buffer_size = AUDIO_MANAGER_OUTPUT_BUFFER_SIZE;
    esp_audio_simple_dec_handle_t decoder = NULL;
    bool decoder_registered = false;
    bool simple_decoder_registered = false;
    bool codec_opened = false;
    bool codec_info_ready = false;
    bool output_task_started = false;
    audio_output_context_t output_ctx = {0};
    TaskHandle_t output_task = NULL;
    esp_err_t result = ESP_OK;
    esp_audio_simple_dec_info_t decoder_info = {0};

    *stopped = false;
    if (is_url) {
        result = audio_manager_http_open(path, &http_client, content_type);
        if (result != ESP_OK) {
            ESP_LOGE(AUDIO_MANAGER_TAG, "Open stream failed: %s",
                     esp_err_to_name(result));
            return result;
        }

        if (audio_manager_is_playlist_url(path, content_type)) {
            char playlist[2048];
            size_t playlist_length = 0;
            while (playlist_length < sizeof(playlist) - 1) {
                int read_length = esp_http_client_read(
                    http_client, playlist + playlist_length,
                    (int)(sizeof(playlist) - 1 - playlist_length));
                if (read_length < 0) {
                    result = ESP_FAIL;
                    break;
                }
                if (read_length == 0) {
                    break;
                }
                playlist_length += (size_t)read_length;
            }
            playlist[playlist_length] = '\0';
            if (result == ESP_OK) {
                result = audio_manager_parse_playlist(
                    playlist, resolved_url, sizeof(resolved_url));
            }

            esp_http_client_close(http_client);
            esp_http_client_cleanup(http_client);
            http_client = NULL;
            if (result != ESP_OK) {
                ESP_LOGE(AUDIO_MANAGER_TAG, "Unable to resolve playlist URL");
                return result;
            }

            result = audio_manager_http_open(
                resolved_url, &http_client, content_type);
            if (result != ESP_OK) {
                return result;
            }
        }

        decoder_type = audio_manager_get_stream_decoder_type(
            resolved_url[0] != '\0' ? resolved_url : path, content_type);
        if (decoder_type == ESP_AUDIO_SIMPLE_DEC_TYPE_NONE) {
            esp_http_client_close(http_client);
            esp_http_client_cleanup(http_client);
            return ESP_ERR_NOT_SUPPORTED;
        }
    } else {
        file = fopen(path, "rb");
        if (file == NULL) {
            ESP_LOGE(AUDIO_MANAGER_TAG, "Open audio file failed: %s", path);
            return ESP_ERR_NOT_FOUND;
        }
    }

    do {
        input_buffer = malloc(AUDIO_MANAGER_INPUT_BUFFER_SIZE);
        output_buffer = malloc(output_buffer_size);
        if (input_buffer == NULL || output_buffer == NULL) {
            result = ESP_ERR_NO_MEM;
            break;
        }

        if (esp_audio_dec_register_default() != ESP_AUDIO_ERR_OK) {
            result = ESP_FAIL;
            break;
        }
        decoder_registered = true;

        if (esp_audio_simple_dec_register_default() != ESP_AUDIO_ERR_OK) {
            result = ESP_FAIL;
            break;
        }
        simple_decoder_registered = true;

        if (esp_audio_simple_check_audio_type(decoder_type) != ESP_AUDIO_ERR_OK) {
            ESP_LOGE(AUDIO_MANAGER_TAG, "Decoder is not enabled for %s", path);
            result = ESP_ERR_NOT_SUPPORTED;
            break;
        }

        esp_audio_simple_dec_cfg_t decoder_config = {
            .dec_type = decoder_type,
            .dec_cfg = NULL,
            .cfg_size = 0,
            .use_frame_dec = false,
        };
        if (esp_audio_simple_dec_open(&decoder_config, &decoder) !=
            ESP_AUDIO_ERR_OK) {
            ESP_LOGE(AUDIO_MANAGER_TAG, "Open decoder failed for %s", path);
            result = ESP_FAIL;
            break;
        }

        ESP_LOGI(AUDIO_MANAGER_TAG, "Playback started: %s", path);
        audio_manager_set_state(AUDIO_MANAGER_STATE_PLAYING);
        audio_manager_emit_event(AUDIO_MANAGER_EVENT_STARTED, ESP_OK);

        while (result == ESP_OK) {
            if (!audio_manager_process_control()) {
                *stopped = true;
                break;
            }

            int stream_read = 0;
            size_t bytes_read = 0;
            if (is_url) {
                stream_read = esp_http_client_read(
                    http_client, (char *)input_buffer,
                    AUDIO_MANAGER_INPUT_BUFFER_SIZE);
                if (stream_read == -ESP_ERR_HTTP_EAGAIN) {
                    continue;
                }
                if (stream_read < 0) {
                    result = ESP_FAIL;
                    break;
                }
                bytes_read = (size_t)stream_read;
            } else {
                bytes_read = fread(
                    input_buffer, 1, AUDIO_MANAGER_INPUT_BUFFER_SIZE, file);
                if (bytes_read == 0 && ferror(file)) {
                    result = ESP_FAIL;
                    break;
                }
            }
            if (bytes_read == 0) {
                break;
            }

            esp_audio_simple_dec_raw_t raw = {
                .buffer = input_buffer,
                .len = (uint32_t)bytes_read,
                .eos = !is_url && bytes_read < AUDIO_MANAGER_INPUT_BUFFER_SIZE,
            };

            while (raw.len > 0) {
                if (!audio_manager_process_control()) {
                    *stopped = true;
                    break;
                }

                esp_audio_simple_dec_out_t decoded = {
                    .buffer = output_buffer,
                    .len = (uint32_t)output_buffer_size,
                };
                esp_audio_err_t decode_err = esp_audio_simple_dec_process(
                    decoder, &raw, &decoded);

                if (decode_err == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                    uint8_t *new_buffer = realloc(
                        output_buffer, decoded.needed_size);
                    if (new_buffer == NULL) {
                        result = ESP_ERR_NO_MEM;
                        break;
                    }
                    output_buffer = new_buffer;
                    output_buffer_size = decoded.needed_size;
                    continue;
                }

                if (decode_err != ESP_AUDIO_ERR_OK) {
                    ESP_LOGE(AUDIO_MANAGER_TAG, "Audio decode failed: %d",
                             (int)decode_err);
                    result = ESP_FAIL;
                    break;
                }

                if (decoded.decoded_size > 0) {
                    if (!codec_info_ready) {
                        if (esp_audio_simple_dec_get_info(
                                decoder, &decoder_info) != ESP_AUDIO_ERR_OK) {
                            result = ESP_FAIL;
                            break;
                        }
                        result = audio_manager_open_codec(&decoder_info);
                        if (result != ESP_OK) {
                            break;
                        }
                        codec_opened = true;
                        codec_info_ready = true;
                    }

                    if (!output_task_started) {
                        output_ctx.queue = xQueueCreate(
                            AUDIO_MANAGER_PCM_QUEUE_LENGTH,
                            sizeof(audio_pcm_block_t *));
                        output_ctx.done = xSemaphoreCreateBinary();
                        output_ctx.error = ESP_OK;
                        output_ctx.codec = s_audio_dac->codec_dev;
                        if (output_ctx.queue == NULL || output_ctx.done == NULL) {
                            result = ESP_ERR_NO_MEM;
                            break;
                        }

                        if (xTaskCreate(
                                audio_manager_output_task,
                                "audio_output",
                                AUDIO_MANAGER_OUTPUT_TASK_STACK_SIZE,
                                &output_ctx,
                                AUDIO_MANAGER_OUTPUT_TASK_PRIORITY,
                                &output_task) != pdPASS) {
                            result = ESP_ERR_NO_MEM;
                            break;
                        }
                        output_task_started = true;
                    }

                    audio_pcm_block_t *block = malloc(
                        sizeof(audio_pcm_block_t) + decoded.decoded_size);
                    if (block == NULL) {
                        result = ESP_ERR_NO_MEM;
                        break;
                    }
                    block->size = decoded.decoded_size;
                    memcpy(block->data, decoded.buffer, decoded.decoded_size);

                    bool block_queued = false;
                    while (!block_queued) {
                        if (xQueueSend(output_ctx.queue, &block,
                                       pdMS_TO_TICKS(100)) == pdTRUE) {
                            block_queued = true;
                            block = NULL; /* Queue/output task owns the block. */
                        } else if (output_ctx.error != ESP_OK) {
                            break;
                        }
                    }

                    if (block != NULL) {
                        free(block);
                    }
                    if (output_ctx.error != ESP_OK) {
                        result = output_ctx.error;
                        ESP_LOGE(AUDIO_MANAGER_TAG, "PCM output failed: %s",
                                 esp_err_to_name(result));
                        break;
                    }
                }

                if (raw.consumed > raw.len) {
                    result = ESP_FAIL;
                    break;
                }
                raw.len -= raw.consumed;
                raw.buffer += raw.consumed;

                if (raw.len > 0 && raw.consumed == 0 &&
                    decoded.decoded_size == 0) {
                    result = ESP_FAIL;
                    break;
                }
            }

            if (*stopped || result != ESP_OK || raw.eos) {
                break;
            }
        }

        if (result == ESP_OK && !*stopped &&
            !audio_manager_process_control()) {
            *stopped = true;
        }

        if (result == ESP_OK && !*stopped && !codec_info_ready) {
            ESP_LOGE(AUDIO_MANAGER_TAG, "No decodable audio data: %s", path);
            result = ESP_FAIL;
        }
    } while (0);

    /*
     * Stop the output task only after all queued PCM has been consumed.
     * This keeps the codec open until the final write has completed.
     */
    if (output_task_started) {
        audio_pcm_block_t *end_marker = NULL;
        while (xQueueSend(output_ctx.queue, &end_marker,
                          pdMS_TO_TICKS(100)) != pdTRUE) {
            /* Wait while the output task drains the queue. */
        }
        xSemaphoreTake(output_ctx.done, portMAX_DELAY);
        if (output_ctx.error != ESP_OK && result == ESP_OK) {
            result = output_ctx.error;
        }
    }

    if (output_ctx.queue != NULL) {
        vQueueDelete(output_ctx.queue);
    }
    if (output_ctx.done != NULL) {
        vSemaphoreDelete(output_ctx.done);
    }

    if (codec_opened) {
        esp_codec_dev_close(s_audio_dac->codec_dev);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_codec_opened = false;
        xSemaphoreGive(s_lock);
    }
    if (decoder != NULL) {
        esp_audio_simple_dec_close(decoder);
    }
    if (simple_decoder_registered) {
        esp_audio_simple_dec_unregister_default();
    }
    if (decoder_registered) {
        esp_audio_dec_unregister_default();
    }
    if (file != NULL) {
        fclose(file);
    }
    if (http_client != NULL) {
        esp_http_client_close(http_client);
        esp_http_client_cleanup(http_client);
    }
    free(input_buffer);
    free(output_buffer);

    return result;
}

static void audio_manager_player_task(void *arg)
{
    (void)arg;

    while (true) {
        uint32_t commands = 0;
        xTaskNotifyWait(0, UINT32_MAX, &commands, portMAX_DELAY);

        if ((commands & AUDIO_COMMAND_EXIT) != 0) {
            xSemaphoreGive(s_task_exit_sem);
            vTaskDelete(NULL);
        }

        if ((commands & AUDIO_COMMAND_PLAY) == 0) {
            continue;
        }

        char path[AUDIO_MANAGER_PATH_MAX];
        bool is_url;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        memcpy(path, s_current_path, sizeof(path));
        is_url = s_current_is_url;
        xSemaphoreGive(s_lock);

        xSemaphoreTake(s_lock, portMAX_DELAY);
        bool cancelled_before_start =
            (s_state == AUDIO_MANAGER_STATE_STOPPING);
        xSemaphoreGive(s_lock);

        bool stopped = cancelled_before_start;
        esp_err_t err = ESP_OK;
        if (!cancelled_before_start) {
            esp_audio_simple_dec_type_t decoder_type = is_url ?
                ESP_AUDIO_SIMPLE_DEC_TYPE_NONE :
                audio_manager_get_decoder_type(path);
            err = audio_manager_play_file_internal(
                path, decoder_type, is_url, &stopped);
        }

        if (stopped) {
            audio_manager_set_state(AUDIO_MANAGER_STATE_STOPPED);
            audio_manager_emit_event(AUDIO_MANAGER_EVENT_STOPPED, ESP_OK);
        } else if (err == ESP_OK) {
            audio_manager_set_state(AUDIO_MANAGER_STATE_FINISHED);
            audio_manager_emit_event(AUDIO_MANAGER_EVENT_FINISHED, ESP_OK);
            ESP_LOGI(AUDIO_MANAGER_TAG, "Playback finished: %s", path);
        } else {
            audio_manager_set_state(AUDIO_MANAGER_STATE_ERROR);
            audio_manager_emit_event(AUDIO_MANAGER_EVENT_ERROR, err);
            ESP_LOGE(AUDIO_MANAGER_TAG, "Playback failed: %s",
                     esp_err_to_name(err));
        }
    }
}
