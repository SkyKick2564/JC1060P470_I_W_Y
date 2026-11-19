#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_codec_dev.h"
#include "esp_player.h"
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
#define VIDEO_RENDER_FPS         30

#define VIDEO_COMMAND_PAUSE  (1UL << 1)
#define VIDEO_COMMAND_RESUME (1UL << 2)
#define VIDEO_COMMAND_STOP   (1UL << 3)

typedef struct {
    EventGroupHandle_t events;
    esp_player_error_source_t error_source;
} media_video_event_context_t;

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

static int media_video_audio_writer(uint8_t *pcm, uint32_t length, void *ctx)
{
    esp_codec_dev_handle_t codec = (esp_codec_dev_handle_t)ctx;
    if (codec == NULL || pcm == NULL || length == 0) {
        return -1;
    }
    return esp_codec_dev_write(codec, pcm, (int)length) == ESP_CODEC_DEV_OK ? 0 : -1;
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
    bool started_event_sent = false;
    bool finished = false;

    esp_gmf_pool_handle_t audio_pool = NULL;
    esp_gmf_pool_handle_t video_pool = NULL;
    esp_audio_render_handle_t audio_render = NULL;
    esp_audio_render_stream_handle_t audio_stream = NULL;
    esp_video_render_handle_t video_render = NULL;
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
            .out_ctx = codec,
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

        esp_player_config_t player_cfg = ESP_PLAYER_CONFIG_DEFAULT();
        player_cfg.audio_render_hd = audio_stream;
        player_cfg.video_render_hd = video_render;
        if (esp_player_init(&player_cfg, &player) != ESP_PLAYER_ERR_OK) {
            result = ESP_FAIL;
            break;
        }

        if (esp_player_set_event_cb(
                player, media_video_player_event, &event_ctx) != ESP_PLAYER_ERR_OK) {
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
            .av_mask = ESP_PLAYER_MASK_AV,
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
