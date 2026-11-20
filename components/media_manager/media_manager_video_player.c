#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_codec_dev.h"
#include "esp_player.h"
#include "esp_player_advance.h"
#include "esp_audio_render.h"
#include "esp_video_render.h"
#include "esp_extractor_defaults.h"
#include "esp_audio_dec_default.h"
#include "esp_video_dec_default.h"
#include "media_lib_adapter.h"

#include "esp_gmf_pool.h"
#include "esp_gmf_element.h"
#include "esp_gmf_ch_cvt.h"
#include "esp_gmf_bit_cvt.h"
#include "esp_gmf_rate_cvt.h"
#include "esp_gmf_alc.h"
#include "esp_gmf_video_color_convert.h"
#if CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31
#include "esp_gmf_video_ppa.h"
#endif

#include "media_manager_video_backend.h"

#define TAG "MEDIA_VIDEO_PLAYER"

#define VIDEO_EVENT_PLAYED       BIT(0)
#define VIDEO_EVENT_PAUSED       BIT(1)
#define VIDEO_EVENT_FINISHED     BIT(2)
#define VIDEO_EVENT_ERROR        BIT(3)
#define VIDEO_EVENT_INFO_PARSED  BIT(4)

#define VIDEO_EVENT_MASK (VIDEO_EVENT_PLAYED | VIDEO_EVENT_PAUSED | \
                          VIDEO_EVENT_FINISHED | VIDEO_EVENT_ERROR | \
                          VIDEO_EVENT_INFO_PARSED)

#define VIDEO_OUTPUT_SAMPLE_RATE 44100
#define VIDEO_OUTPUT_BITS        16
#define VIDEO_OUTPUT_CHANNELS    2
#define VIDEO_RENDER_FPS         25

#define VIDEO_AUDIO_QUEUE_LENGTH       24
#define VIDEO_AUDIO_PCM_BLOCK_SIZE     (64 * 1024)
#define VIDEO_AUDIO_OUTPUT_TASK_STACK  (4 * 1024)
#define VIDEO_AUDIO_OUTPUT_TASK_PRIO   (configMAX_PRIORITIES - 3)
#define VIDEO_AUDIO_OUTPUT_WAIT        pdMS_TO_TICKS(100)

/*
 * Keep the CPU-heavy H.264 decode worker on Core 1 (sdkconfig). Run video
 * color conversion/rendering on Core 0 so it does not starve LVGL on Core 1.
 * Keep audio output on Core 1 and give audio stages sufficient priority.
 */
static const esp_player_task_config_t s_player_task_config = {
    .extractor = {
        .stack = 5120,
        .prio = 6,
        .core = 0,
        .stack_in_ext = 1,
    },
    .audio_decoder = {
        .stack = 5120,
        .prio = 7,
        .core = 1,
        .stack_in_ext = 1,
    },
    .audio_render = {
        .stack = 5120,
        .prio = 8,
        .core = 0,
        .stack_in_ext = 1,
    },
    .video_decoder = {
        .stack = 5120,
        .prio = 6,
        .core = 0,
        .stack_in_ext = 1,
    },
    .video_render = {
        .stack = 5120,
        .prio = 6,
        .core = 0,
        .stack_in_ext = 1,
    },
};

#define VIDEO_COMMAND_PAUSE  (1UL << 1)
#define VIDEO_COMMAND_RESUME (1UL << 2)
#define VIDEO_COMMAND_STOP   (1UL << 3)

typedef struct {
    EventGroupHandle_t events;
    esp_player_error_source_t error_source;
} media_video_event_context_t;

typedef struct {
    size_t size;
    uint8_t data[VIDEO_AUDIO_PCM_BLOCK_SIZE];
} media_video_pcm_block_t;

typedef struct {
    QueueHandle_t queue;
    QueueHandle_t free_queue;
    SemaphoreHandle_t done;
    esp_codec_dev_handle_t codec;
    volatile esp_err_t error;
} media_video_audio_output_t;

static void media_video_audio_output_task(void *arg)
{
    media_video_audio_output_t *output = (media_video_audio_output_t *)arg;
    media_video_pcm_block_t *block = NULL;

    while (xQueueReceive(output->queue, &block, portMAX_DELAY) == pdTRUE) {
        if (block == NULL) {
            break;
        }

        if (output->error == ESP_OK) {
            esp_err_t err = esp_codec_dev_write(
                output->codec, block->data, (int)block->size);
            if (err != ESP_CODEC_DEV_OK) {
                output->error = err;
                ESP_LOGE(TAG, "Audio output write failed: %s",
                         esp_err_to_name(err));
            }
        }

        if (xQueueSend(output->free_queue, &block, 0) != pdTRUE) {
            ESP_LOGE(TAG, "Could not return PCM block to free pool");
        }
    }

    xSemaphoreGive(output->done);
    vTaskDelete(NULL);
}

static int media_video_audio_writer(uint8_t *pcm, uint32_t length, void *ctx)
{
    media_video_audio_output_t *output =
        (media_video_audio_output_t *)ctx;
    media_video_pcm_block_t *block = NULL;

    if (output == NULL || output->queue == NULL ||
        output->free_queue == NULL || pcm == NULL || length == 0 ||
        length > VIDEO_AUDIO_PCM_BLOCK_SIZE) {
        ESP_LOGE(TAG, "Invalid PCM block length: %" PRIu32, length);
        return -1;
    }

    /*
     * Apply backpressure when the decoder produces PCM faster than the codec
     * consumes it. Keep waiting for a returned block instead of failing
     * playback merely because all preallocated blocks are temporarily busy.
     * Poll periodically so a codec write error can abort the wait promptly.
     */
    while (xQueueReceive(output->free_queue, &block,
                         VIDEO_AUDIO_OUTPUT_WAIT) != pdTRUE) {
        if (output->error != ESP_OK) {
            ESP_LOGE(TAG, "Audio output stopped while waiting for a PCM block: %s",
                     esp_err_to_name(output->error));
            return -1;
        }
    }

    if (block == NULL) {
        ESP_LOGE(TAG, "Received an invalid PCM block from the free pool");
        return -1;
    }

    if (output->error != ESP_OK) {
        (void)xQueueSend(output->free_queue, &block, 0);
        ESP_LOGE(TAG, "Audio output is in error state: %s",
                 esp_err_to_name(output->error));
        return -1;
    }

    block->size = length;
    memcpy(block->data, pcm, length);

    if (xQueueSend(output->queue, &block, VIDEO_AUDIO_OUTPUT_WAIT) != pdTRUE) {
        (void)xQueueSend(output->free_queue, &block, 0);
        ESP_LOGW(TAG, "Audio output queue is full");
        return -1;
    }

    return 0;
}

static esp_err_t media_video_register_audio_pool(esp_gmf_pool_handle_t *pool)
{
    esp_gmf_element_handle_t element = NULL;

    *pool = NULL;
    if (esp_gmf_pool_init(pool) != ESP_GMF_ERR_OK) {
        return ESP_FAIL;
    }

    esp_ae_ch_cvt_cfg_t channel_cfg = DEFAULT_ESP_GMF_CH_CVT_CONFIG();
    if (esp_gmf_ch_cvt_init(&channel_cfg, &element) != ESP_GMF_ERR_OK ||
        element == NULL) {
        return ESP_FAIL;
    }
    if (esp_gmf_pool_register_element(*pool, element, NULL) != ESP_GMF_ERR_OK) {
        esp_gmf_element_deinit(element);
        return ESP_FAIL;
    }

    element = NULL;
    esp_ae_bit_cvt_cfg_t bit_cfg = DEFAULT_ESP_GMF_BIT_CVT_CONFIG();
    if (esp_gmf_bit_cvt_init(&bit_cfg, &element) != ESP_GMF_ERR_OK ||
        element == NULL) {
        return ESP_FAIL;
    }
    if (esp_gmf_pool_register_element(*pool, element, NULL) != ESP_GMF_ERR_OK) {
        esp_gmf_element_deinit(element);
        return ESP_FAIL;
    }

    element = NULL;
    esp_ae_rate_cvt_cfg_t rate_cfg = DEFAULT_ESP_GMF_RATE_CVT_CONFIG();
    if (esp_gmf_rate_cvt_init(&rate_cfg, &element) != ESP_GMF_ERR_OK ||
        element == NULL) {
        return ESP_FAIL;
    }
    if (esp_gmf_pool_register_element(*pool, element, NULL) != ESP_GMF_ERR_OK) {
        esp_gmf_element_deinit(element);
        return ESP_FAIL;
    }

    element = NULL;
    esp_ae_alc_cfg_t alc_cfg = DEFAULT_ESP_GMF_ALC_CONFIG();
    if (esp_gmf_alc_init(&alc_cfg, &element) != ESP_GMF_ERR_OK ||
        element == NULL) {
        return ESP_FAIL;
    }
    if (esp_gmf_pool_register_element(*pool, element, NULL) != ESP_GMF_ERR_OK) {
        esp_gmf_element_deinit(element);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t media_video_register_video_pool(esp_gmf_pool_handle_t *pool)
{
    esp_gmf_element_handle_t element = NULL;

    *pool = NULL;
    if (esp_gmf_pool_init(pool) != ESP_GMF_ERR_OK) {
        return ESP_FAIL;
    }

#if CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31
    if (esp_gmf_video_ppa_init(NULL, &element) != ESP_GMF_ERR_OK ||
        element == NULL) {
        return ESP_FAIL;
    }
    if (esp_gmf_pool_register_element(*pool, element, NULL) != ESP_GMF_ERR_OK) {
        esp_gmf_element_deinit(element);
        return ESP_FAIL;
    }
#endif

    element = NULL;
    esp_imgfx_color_convert_cfg_t color_cfg = {
        .color_space_std = ESP_IMGFX_COLOR_SPACE_STD_BT601,
    };
    if (esp_gmf_video_color_convert_init(&color_cfg, &element) != ESP_GMF_ERR_OK ||
        element == NULL) {
        return ESP_FAIL;
    }
    if (esp_gmf_pool_register_element(*pool, element, NULL) != ESP_GMF_ERR_OK) {
        esp_gmf_element_deinit(element);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_player_err_t media_video_player_event(
    esp_player_event_msg_t *message,
    void *user_ctx)
{
    media_video_event_context_t *ctx = (media_video_event_context_t *)user_ctx;
    if (ctx == NULL || ctx->events == NULL || message == NULL) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }

    switch (message->event_type) {
    case ESP_PLAYER_EVENT_PLAYED:
        xEventGroupSetBits(ctx->events, VIDEO_EVENT_PLAYED);
        break;
    case ESP_PLAYER_EVENT_PAUSED:
        xEventGroupSetBits(ctx->events, VIDEO_EVENT_PAUSED);
        break;
    case ESP_PLAYER_EVENT_TRACK_INFO_PARSED:
        break;
    case ESP_PLAYER_EVENT_VIDEO_INFO_PARSED:
        xEventGroupSetBits(ctx->events, VIDEO_EVENT_INFO_PARSED);
        break;
    case ESP_PLAYER_EVENT_FINISHED:
        xEventGroupSetBits(ctx->events, VIDEO_EVENT_FINISHED);
        break;
    case ESP_PLAYER_EVENT_ERROR:
        if (message->data != NULL &&
            message->data_len >= sizeof(ctx->error_source)) {
            ctx->error_source = *(esp_player_error_source_t *)message->data;
        }
        xEventGroupSetBits(ctx->events, VIDEO_EVENT_ERROR);
        break;
    default:
        break;
    }

    return ESP_PLAYER_ERR_OK;
}

static esp_err_t media_video_set_source_info(esp_player_handle_t player)
{
    for (int attempt = 0; attempt < 20; attempt++) {
        uint16_t track_count = 0;
        esp_player_err_t player_err = esp_player_get_track_num(
            player, ESP_PLAYER_TRACK_TYPE_VIDEO, &track_count);

        if (player_err == ESP_PLAYER_ERR_OK && track_count == 0) {
            (void)media_manager_video_backend_set_source_info(0, 0);
            return ESP_OK;
        }

        if (player_err == ESP_PLAYER_ERR_OK && track_count > 0) {
            esp_player_track_info_t track_info = {0};
            player_err = esp_player_get_track_info(
                player, ESP_PLAYER_TRACK_TYPE_VIDEO, 0, &track_info);
            if (player_err == ESP_PLAYER_ERR_OK) {
                return media_manager_video_backend_set_source_info(
                    track_info.video_info.width,
                    track_info.video_info.height);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGE(TAG, "Could not retrieve parsed video track dimensions");
    (void)media_manager_video_backend_set_source_info(0, 0);
    return ESP_FAIL;
}

static void media_video_register_defaults(void)
{
    media_lib_add_default_adapter();
    esp_extractor_register_default();
    esp_audio_dec_register_default();
    esp_video_dec_register_default();
    ESP_LOGI(TAG, "Espressif default H.264 decoder registered");
}

static void media_video_unregister_defaults(void)
{
    esp_video_dec_unregister_default();
    esp_audio_dec_unregister_default();
    esp_extractor_unregister_default();
}

esp_err_t media_manager_video_play_file(
    const char *path,
    esp_codec_dev_handle_t codec,
    float volume,
    media_manager_video_event_callback_t event_callback,
    void *event_user_ctx,
    bool *stopped)
{
    esp_err_t result = ESP_OK;
    bool codec_opened = false;
    bool media_defaults_registered = false;
    bool player_started = false;
    bool video_enabled = false;
    bool started_event_sent = false;
    bool finished = false;

    esp_gmf_pool_handle_t audio_pool = NULL;
    esp_gmf_pool_handle_t video_pool = NULL;
    esp_audio_render_handle_t audio_render = NULL;
    esp_audio_render_stream_handle_t audio_stream = NULL;
    esp_video_render_handle_t video_render = NULL;
    media_video_audio_output_t audio_output = {0};
    media_video_pcm_block_t *audio_block_pool = NULL;
    TaskHandle_t audio_output_task = NULL;
    bool audio_output_task_started = false;
    esp_player_handle_t player = NULL;
    EventGroupHandle_t events = NULL;
    media_video_event_context_t event_ctx = {0};

    char url[MEDIA_MANAGER_PATH_MAX + 16] = {0};

    if (stopped != NULL) {
        *stopped = false;
    }
    if (path == NULL || codec == NULL || stopped == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    events = xEventGroupCreate();
    if (events == NULL) {
        return ESP_ERR_NO_MEM;
    }
    event_ctx.events = events;

    do {
        media_video_register_defaults();
        media_defaults_registered = true;

        esp_codec_dev_sample_info_t output_info = {
            .sample_rate = VIDEO_OUTPUT_SAMPLE_RATE,
            .channel = VIDEO_OUTPUT_CHANNELS,
            .bits_per_sample = VIDEO_OUTPUT_BITS,
        };
        if (esp_codec_dev_open(codec, &output_info) != ESP_CODEC_DEV_OK) {
            result = ESP_FAIL;
            break;
        }
        codec_opened = true;
        media_manager_notify_codec_opened(true);

        audio_output.codec = codec;
        audio_output.error = ESP_OK;
        audio_output.queue = xQueueCreate(
            VIDEO_AUDIO_QUEUE_LENGTH, sizeof(media_video_pcm_block_t *));
        audio_output.free_queue = xQueueCreate(
            VIDEO_AUDIO_QUEUE_LENGTH, sizeof(media_video_pcm_block_t *));
        audio_output.done = xSemaphoreCreateBinary();
        audio_block_pool = heap_caps_calloc(
            VIDEO_AUDIO_QUEUE_LENGTH,
            sizeof(*audio_block_pool),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (audio_output.queue == NULL || audio_output.free_queue == NULL ||
            audio_output.done == NULL || audio_block_pool == NULL) {
            result = ESP_ERR_NO_MEM;
            break;
        }

        for (size_t i = 0; i < VIDEO_AUDIO_QUEUE_LENGTH; i++) {
            media_video_pcm_block_t *block = &audio_block_pool[i];
            if (xQueueSend(audio_output.free_queue, &block, 0) != pdTRUE) {
                result = ESP_FAIL;
                break;
            }
        }
        if (result != ESP_OK) {
            break;
        }

        if (xTaskCreatePinnedToCore(
                media_video_audio_output_task,
                "media_audio_out",
                VIDEO_AUDIO_OUTPUT_TASK_STACK,
                &audio_output,
                VIDEO_AUDIO_OUTPUT_TASK_PRIO,
                &audio_output_task,
                1) != pdPASS) {
            result = ESP_ERR_NO_MEM;
            break;
        }
        audio_output_task_started = true;

        if (esp_codec_dev_set_out_vol(codec, volume) != ESP_CODEC_DEV_OK) {
            ESP_LOGW(TAG, "Could not set initial video playback volume");
        }

        result = media_video_register_audio_pool(&audio_pool);
        if (result != ESP_OK) {
            break;
        }

        esp_audio_render_cfg_t audio_cfg = {
            .max_stream_num = 1,
            .out_writer = media_video_audio_writer,
            .out_ctx = &audio_output,
            .out_sample_info = {
                .sample_rate = VIDEO_OUTPUT_SAMPLE_RATE,
                .bits_per_sample = VIDEO_OUTPUT_BITS,
                .channel = VIDEO_OUTPUT_CHANNELS,
            },
            .pool = audio_pool,
            .process_period = 20,
        };
        if (esp_audio_render_create(&audio_cfg, &audio_render) !=
            ESP_AUDIO_RENDER_ERR_OK) {
            result = ESP_FAIL;
            break;
        }

        if (esp_audio_render_stream_get(
                audio_render, ESP_AUDIO_RENDER_FIRST_STREAM,
                &audio_stream) != ESP_AUDIO_RENDER_ERR_OK) {
            result = ESP_FAIL;
            break;
        }

        video_enabled = media_manager_video_backend_has_target();
        if (video_enabled) {
            result = media_video_register_video_pool(&video_pool);
            if (result != ESP_OK) {
                break;
            }

            esp_video_render_cfg_t video_cfg = {
                .pool = video_pool,
                .fps = VIDEO_RENDER_FPS,
            };
            if (esp_video_render_create(&video_cfg, &video_render) !=
                ESP_VIDEO_RENDER_ERR_OK) {
                result = ESP_FAIL;
                break;
            }

            result = media_manager_video_backend_attach(video_render);
            if (result != ESP_OK) {
                break;
            }
            ESP_LOGI(TAG, "Video output enabled; EEZ Image target is ready");
        } else {
            ESP_LOGI(TAG, "No EEZ Image target; playing MP4 audio track only");
        }

        esp_player_config_t player_cfg = ESP_PLAYER_CONFIG_DEFAULT();
        player_cfg.audio_render_hd = audio_stream;
        player_cfg.video_render_hd = video_enabled ? video_render : NULL;
        if (esp_player_init(&player_cfg, &player) != ESP_PLAYER_ERR_OK) {
            result = ESP_FAIL;
            break;
        }

        if (esp_player_set_event_cb(
                player, media_video_player_event, &event_ctx) != ESP_PLAYER_ERR_OK) {
            result = ESP_FAIL;
            break;
        }

        if (esp_player_set_task_config(player, &s_player_task_config) !=
            ESP_PLAYER_ERR_OK) {
            ESP_LOGE(TAG, "Configure playback task affinity failed");
            result = ESP_FAIL;
            break;
        }

        if (strncmp(path, "file://", 7) == 0) {
            snprintf(url, sizeof(url), "%s", path);
        } else {
            snprintf(url, sizeof(url), "file://%s", path);
        }

        esp_player_data_src_t source = {
            .url = url,
            .av_mask = video_enabled ? ESP_PLAYER_MASK_AV : ESP_PLAYER_MASK_AUDIO,
            .sync_mode = ESP_PLAYER_SYNC_MODE_AUDIO,
        };
        if (esp_player_set_data_src(player, &source) != ESP_PLAYER_ERR_OK ||
            esp_player_run(player) != ESP_PLAYER_ERR_OK) {
            result = ESP_FAIL;
            break;
        }
        player_started = true;

        while (!finished) {
            EventBits_t bits = xEventGroupWaitBits(
                events,
                VIDEO_EVENT_MASK,
                pdTRUE,
                pdFALSE,
                pdMS_TO_TICKS(50));

            if ((bits & VIDEO_EVENT_INFO_PARSED) != 0) {
                result = media_video_set_source_info(player);
                if (result != ESP_OK) {
                    ESP_LOGE(TAG, "Video track setup failed: %s",
                             esp_err_to_name(result));
                    (void)esp_player_stop(player);
                    finished = true;
                    break;
                }
            }

            if ((bits & VIDEO_EVENT_PLAYED) != 0) {
                if (event_callback != NULL) {
                    event_callback(
                        started_event_sent ? MEDIA_MANAGER_EVENT_RESUMED :
                                             MEDIA_MANAGER_EVENT_STARTED,
                        ESP_OK,
                        event_user_ctx);
                }
                started_event_sent = true;
            }

            if ((bits & VIDEO_EVENT_PAUSED) != 0 && event_callback != NULL) {
                event_callback(MEDIA_MANAGER_EVENT_PAUSED, ESP_OK, event_user_ctx);
            }

            if ((bits & VIDEO_EVENT_ERROR) != 0) {
                ESP_LOGE(TAG, "ESP Player error source: %d",
                         (int)event_ctx.error_source);
                result = ESP_FAIL;
                finished = true;
                break;
            }

            if ((bits & VIDEO_EVENT_FINISHED) != 0) {
                result = ESP_OK;
                finished = true;
                break;
            }

            uint32_t commands = 0;
            xTaskNotifyWait(0, UINT32_MAX, &commands, 0);

            if ((commands & VIDEO_COMMAND_STOP) != 0) {
                (void)esp_player_stop(player);
                *stopped = true;
                finished = true;
            } else if ((commands & VIDEO_COMMAND_PAUSE) != 0) {
                if (esp_player_pause(player) != ESP_PLAYER_ERR_OK) {
                    result = ESP_FAIL;
                    (void)esp_player_stop(player);
                    finished = true;
                }
            } else if ((commands & VIDEO_COMMAND_RESUME) != 0) {
                if (esp_player_resume(player) != ESP_PLAYER_ERR_OK) {
                    result = ESP_FAIL;
                    (void)esp_player_stop(player);
                    finished = true;
                }
            }
        }
    } while (0);

    if (player != NULL) {
        if (player_started && !*stopped && result != ESP_OK) {
            (void)esp_player_stop(player);
        }
        (void)esp_player_set_event_cb(player, NULL, NULL);
        (void)esp_player_deinit(player);
    }

    if (video_render != NULL) {
        (void)esp_video_render_destroy(video_render);
    }
    if (video_pool != NULL) {
        (void)esp_gmf_pool_deinit(video_pool);
    }

    if (audio_render != NULL) {
        (void)esp_audio_render_destroy(audio_render);
    }
    if (audio_pool != NULL) {
        (void)esp_gmf_pool_deinit(audio_pool);
    }

    /*
     * The player and audio renderer are stopped before the output task is
     * drained, so no more PCM blocks can be submitted after this marker.
     */
    if (audio_output_task_started) {
        media_video_pcm_block_t *end_marker = NULL;
        while (xQueueSend(audio_output.queue, &end_marker,
                          pdMS_TO_TICKS(100)) != pdTRUE) {
            /* Let the output task drain queued PCM before retrying. */
        }
        (void)xSemaphoreTake(audio_output.done, portMAX_DELAY);
        if (audio_output.error != ESP_OK && result == ESP_OK) {
            result = audio_output.error;
        }
    }

    heap_caps_free(audio_block_pool);

    if (audio_output.queue != NULL) {
        vQueueDelete(audio_output.queue);
    }
    if (audio_output.free_queue != NULL) {
        vQueueDelete(audio_output.free_queue);
    }
    if (audio_output.done != NULL) {
        vSemaphoreDelete(audio_output.done);
    }

    if (codec_opened) {
        media_manager_notify_codec_opened(false);
        (void)esp_codec_dev_close(codec);
    }

    if (media_defaults_registered) {
        media_video_unregister_defaults();
    }

    if (events != NULL) {
        vEventGroupDelete(events);
    }

    return result;
}
