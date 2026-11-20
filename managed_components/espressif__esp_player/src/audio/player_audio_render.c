/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "player_audio_render.h"

static const char *TAG = "ESP_PLAYER_AUDIO_RENDER";

typedef struct {
    int64_t window_start_us;
    uint32_t calls, blocks;
    uint64_t acquire_us, write_us, release_us, process_us, bytes, max_acquire_us;
} player_audio_render_profile_t;
static player_audio_render_profile_t s_audio_render_profile;

static void player_audio_render_profile_add(uint64_t acquire_us, uint64_t write_us,
                                            uint64_t release_us, uint64_t process_us, uint32_t bytes, bool written)
{
    int64_t now_us = esp_timer_get_time();
    player_audio_render_profile_t *p = &s_audio_render_profile;
    if (p->window_start_us == 0) p->window_start_us = now_us;
    p->calls++;
    p->acquire_us += acquire_us;
    p->write_us += write_us;
    p->release_us += release_us;
    p->process_us += process_us;
    if (acquire_us > p->max_acquire_us) p->max_acquire_us = acquire_us;
    if (written) { p->blocks++; p->bytes += bytes; }
    uint64_t elapsed_us = (uint64_t)(now_us - p->window_start_us);
    if (elapsed_us < 1000000ULL) return;
    uint64_t rate_x100 = (uint64_t)p->blocks * 100000000ULL / elapsed_us;
    ESP_LOGI(TAG, "PROFILE audio_render: blocks/s=%llu.%02llu blocks=%u calls=%u avg_acquire=%lluus max_acquire=%lluus avg_write=%lluus avg_release=%lluus avg_other=%lluus bytes/s=%llu",
             (unsigned long long)(rate_x100 / 100ULL), (unsigned long long)(rate_x100 % 100ULL),
             (unsigned)p->blocks, (unsigned)p->calls,
             (unsigned long long)(p->calls ? p->acquire_us / p->calls : 0),
             (unsigned long long)p->max_acquire_us,
             (unsigned long long)(p->blocks ? p->write_us / p->blocks : 0),
             (unsigned long long)(p->calls ? p->release_us / p->calls : 0),
             (unsigned long long)(p->calls ? (p->process_us > p->acquire_us + p->write_us + p->release_us ?
                 (p->process_us - p->acquire_us - p->write_us - p->release_us) / p->calls : 0) : 0),
             (unsigned long long)(p->bytes * 1000000ULL / elapsed_us));
    p->window_start_us = now_us;
    p->calls = p->blocks = 0;
    p->acquire_us = p->write_us = p->release_us = p->process_us = p->bytes = p->max_acquire_us = 0;
}

typedef struct {
    esp_gmf_element_t  parent;      /*!< Parent element */
    bool               is_paused;   /*!< Is paused */
    bool               is_seeking;  /*!< Is seeking */
    uint32_t           frame_size;  /*!< Bytes to acquire per process call; set via player_audio_render_set_frame_duration() */
} player_audio_render_t;

static esp_gmf_job_err_t player_audio_render_open(esp_gmf_element_handle_t self, void *para)
{
    esp_audio_render_err_t render_ret = ESP_AUDIO_RENDER_ERR_OK;
    player_audio_render_config_t *cfg = (player_audio_render_config_t *)OBJ_GET_CFG(self);
    if (cfg == NULL || cfg->stream_handle == NULL) {
        ESP_LOGE(TAG, "Invalid configuration or stream handle");
        return ESP_GMF_JOB_ERR_FAIL;
    }
    render_ret = esp_audio_render_stream_open(cfg->stream_handle, &cfg->sample_info);
    if (render_ret != ESP_AUDIO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "Open audio render stream error, ret: %d", render_ret);
        return ESP_GMF_JOB_ERR_FAIL;
    }
    ESP_LOGI(TAG, "Audio render opened successfully");
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t player_audio_render_close(esp_gmf_element_handle_t self, void *para)
{
    esp_audio_render_err_t render_ret = ESP_AUDIO_RENDER_ERR_OK;
    player_audio_render_config_t *cfg = (player_audio_render_config_t *)OBJ_GET_CFG(self);
    if (cfg && cfg->stream_handle) {
        render_ret = esp_audio_render_stream_close(cfg->stream_handle);
        if (render_ret != ESP_AUDIO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Close audio render stream error, ret: %d", render_ret);
        }
    } else {
        ESP_LOGW(TAG, "Audio render stream handle is NULL, skipping close");
    }

    ESP_LOGI(TAG, "Audio render closed successfully");
    return ESP_GMF_JOB_ERR_OK;
}

static inline bool player_audio_render_abort_during_seek(const player_audio_render_config_t *cfg, esp_gmf_err_io_t io_ret)
{
    return io_ret == ESP_GMF_IO_ABORT && cfg && cfg->sync_handle && player_sync_get_seek_in_progress(cfg->sync_handle);
}

static esp_gmf_job_err_t player_audio_render_process(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_job_err_t job_ret = ESP_GMF_JOB_ERR_OK;
    esp_audio_render_err_t render_ret = ESP_AUDIO_RENDER_ERR_OK;
    esp_gmf_port_handle_t in_port = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_payload_t *in_load = NULL;

    if (in_port == NULL) {
        ESP_LOGE(TAG, "No input port");
        return ESP_GMF_JOB_ERR_FAIL;
    }
    player_audio_render_config_t *cfg = (player_audio_render_config_t *)OBJ_GET_CFG(self);
    if (cfg == NULL || cfg->stream_handle == NULL) {
        ESP_LOGE(TAG, "Invalid render config");
        return ESP_GMF_JOB_ERR_FAIL;
    }
    player_audio_render_t *audio_render = (player_audio_render_t *)self;
    int64_t process_start_us = esp_timer_get_time();
    uint64_t acquire_us = 0, write_us = 0, release_us = 0;
    uint32_t written_bytes = 0;
    bool written = false;
    int64_t stage_start_us = esp_timer_get_time();
    esp_gmf_err_io_t io_ret = esp_gmf_port_acquire_in(in_port, &in_load, audio_render->frame_size, ESP_GMF_MAX_DELAY);
    acquire_us = (uint64_t)(esp_timer_get_time() - stage_start_us);
    if (player_audio_render_abort_during_seek(cfg, io_ret)) {
        vTaskDelay(1);
        return ESP_GMF_JOB_ERR_CONTINUE;
    }
    ESP_GMF_PORT_ACQUIRE_IN_CHECK(TAG, io_ret, job_ret, goto _exit);
    if (audio_render->is_seeking == true) {
        job_ret = ESP_GMF_JOB_ERR_OK;
        vTaskDelay(1);
        goto _exit;
    }
    if (cfg->sync_handle && !in_load->is_done && in_load->valid_size > 0) {
        if (!player_sync_audio_render_frame(cfg->sync_handle, in_load->pts)) {
            ESP_LOGD(TAG, "Drop PCM at render (PTS %llu)", (unsigned long long)in_load->pts);
            goto _exit;
        }
    }
    if (in_load->valid_size == 0) {
        // Skip empty payload to avoid write invalid arg
        if (in_load->is_done == false) {
            job_ret = ESP_GMF_JOB_ERR_CONTINUE;
        }
        goto _exit;
    }
    if (in_load->buf == NULL && in_load->valid_size > 0) {
        ESP_LOGW(TAG, "Skip write: buf NULL, valid_size=%u", (unsigned)in_load->valid_size);
        goto _exit;
    }
    written_bytes = in_load->valid_size;
    stage_start_us = esp_timer_get_time();
    render_ret = esp_audio_render_stream_write(cfg->stream_handle,
                                               in_load->buf,
                                               in_load->valid_size);
    write_us = (uint64_t)(esp_timer_get_time() - stage_start_us);
    written = (render_ret == ESP_AUDIO_RENDER_ERR_OK);
    if (render_ret != ESP_AUDIO_RENDER_ERR_OK && render_ret != ESP_AUDIO_RENDER_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Write audio render stream error, ret: %d", render_ret);
        job_ret = ESP_GMF_JOB_ERR_FAIL;
        goto _exit;
    }
    // Check if completed

_exit:
    // Release input data
    if (in_load) {
        if (in_load->is_done && job_ret == ESP_GMF_JOB_ERR_OK) {
            if (audio_render->is_seeking || (cfg->sync_handle && player_sync_get_seek_in_progress(cfg->sync_handle))) {
                vTaskDelay(1);
            } else {
                job_ret = ESP_GMF_JOB_ERR_DONE;
            }
        }
        stage_start_us = esp_timer_get_time();
        io_ret = esp_gmf_port_release_in(in_port, in_load, ESP_GMF_MAX_DELAY);
        release_us = (uint64_t)(esp_timer_get_time() - stage_start_us);
        if (player_audio_render_abort_during_seek(cfg, io_ret)) {
            return ESP_GMF_JOB_ERR_CONTINUE;
        }
        ESP_GMF_PORT_RELEASE_IN_CHECK(TAG, io_ret, job_ret, return job_ret);
    }
    player_audio_render_profile_add(acquire_us, write_us, release_us,
                                    (uint64_t)(esp_timer_get_time() - process_start_us),
                                    written_bytes, written);

    return job_ret;
}

static esp_gmf_err_t player_audio_render_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return player_audio_render_init((player_audio_render_config_t *)cfg, handle);
}

static esp_gmf_err_t player_audio_render_destroy(esp_gmf_element_handle_t self)
{
    ESP_LOGD(TAG, "Destroyed, %p", self);
    void *cfg = OBJ_GET_CFG(self);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    esp_gmf_element_deinit(self);
    esp_gmf_oal_free(self);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t player_audio_render_init(player_audio_render_config_t *config, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG;);
    *handle = NULL;

    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    player_audio_render_t *audio_render = NULL;
    player_audio_render_config_t *cfg = NULL;

    // Allocate audio renderer
    audio_render = esp_gmf_oal_calloc(1, sizeof(player_audio_render_t));
    ESP_GMF_MEM_VERIFY(TAG, audio_render, {return ESP_GMF_ERR_MEMORY_LACK;}, "audio_render", sizeof(player_audio_render_t));
    audio_render->is_seeking = false;
    audio_render->is_paused = false;
    // Allocate configuration
    cfg = esp_gmf_oal_calloc(1, sizeof(player_audio_render_config_t));
    ESP_GMF_MEM_VERIFY(TAG, cfg, {ret = ESP_GMF_ERR_MEMORY_LACK; goto AUDIO_RENDER_INIT_FAIL;}, "audio_render configuration", sizeof(player_audio_render_config_t));

    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)audio_render;
    obj->new_obj = player_audio_render_new;
    obj->del_obj = player_audio_render_destroy;
    esp_gmf_obj_set_config(obj, cfg, sizeof(player_audio_render_config_t));
    if (config) {
        memcpy(cfg, config, sizeof(player_audio_render_config_t));
    } else {
        player_audio_render_config_t dcfg = DEFAULT_PLAYER_AUDIO_RENDER_CONFIG();
        memcpy(cfg, &dcfg, sizeof(player_audio_render_config_t));
    }
    ret = esp_gmf_obj_set_tag(obj, "aud_render");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto AUDIO_RENDER_INIT_FAIL, "Failed to set obj tag");
    esp_gmf_element_cfg_t el_cfg = {0};
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    el_cfg.dependency = false;
    ret = esp_gmf_element_init(audio_render, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto AUDIO_RENDER_INIT_FAIL, "Failed to initialize audio render element");
    ESP_GMF_ELEMENT_GET(audio_render)->ops.open = player_audio_render_open;
    ESP_GMF_ELEMENT_GET(audio_render)->ops.process = player_audio_render_process;
    ESP_GMF_ELEMENT_GET(audio_render)->ops.close = player_audio_render_close;
    *handle = obj;
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(obj), obj);
    return ESP_GMF_ERR_OK;

AUDIO_RENDER_INIT_FAIL:
    player_audio_render_destroy((esp_gmf_obj_t *)audio_render);
    return ret;
}

esp_gmf_err_t player_audio_render_set_sample_info(esp_gmf_element_handle_t handle,
                                                  const esp_audio_render_sample_info_t *sample_info)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG;);
    ESP_GMF_NULL_CHECK(TAG, sample_info, return ESP_GMF_ERR_INVALID_ARG;);
    if (sample_info->sample_rate == 0 || sample_info->channel == 0 || sample_info->bits_per_sample == 0) {
        ESP_LOGE(TAG, "Invalid sample info");
        return ESP_GMF_ERR_INVALID_ARG;
    }
    player_audio_render_config_t *cfg = (player_audio_render_config_t *)OBJ_GET_CFG(handle);
    if (cfg == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    if (cfg->sample_info.sample_rate != sample_info->sample_rate
        || cfg->sample_info.bits_per_sample != sample_info->bits_per_sample
        || cfg->sample_info.channel != sample_info->channel) {
        ESP_LOGI(TAG, "Refresh sample info: %lu Hz/%u bits/%u ch -> %lu Hz/%u bits/%u ch",
                 (unsigned long)cfg->sample_info.sample_rate, cfg->sample_info.bits_per_sample, cfg->sample_info.channel,
                 (unsigned long)sample_info->sample_rate, sample_info->bits_per_sample, sample_info->channel);
        cfg->sample_info.sample_rate = sample_info->sample_rate;
        cfg->sample_info.bits_per_sample = sample_info->bits_per_sample;
        cfg->sample_info.channel = sample_info->channel;
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t player_audio_render_set_frame_duration(esp_gmf_element_handle_t handle, uint32_t duration_ms)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG;);
    if (duration_ms == 0) {
        ESP_LOGE(TAG, "Invalid frame duration: 0 ms");
        return ESP_GMF_ERR_INVALID_ARG;
    }
    player_audio_render_config_t *cfg = (player_audio_render_config_t *)OBJ_GET_CFG(handle);
    if (cfg == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    const esp_audio_render_sample_info_t *info = &cfg->sample_info;
    if (info->sample_rate == 0 || info->channel == 0 || info->bits_per_sample == 0) {
        ESP_LOGE(TAG, "Invalid sample info");
        return ESP_GMF_ERR_INVALID_ARG;
    }
    uint32_t bytes_per_sample = (uint32_t)info->bits_per_sample >> 3;
    uint64_t frame_size = (uint64_t)info->sample_rate * info->channel * bytes_per_sample * duration_ms / 1000;
    if (frame_size == 0 || frame_size > UINT32_MAX) {
        ESP_LOGE(TAG, "Calculated frame size out of range: %llu", (unsigned long long)frame_size);
        return ESP_GMF_ERR_INVALID_ARG;
    }
    player_audio_render_t *audio_render = (player_audio_render_t *)handle;
    audio_render->frame_size = (uint32_t)frame_size;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t player_audio_render_set_speed(esp_gmf_element_handle_t handle, float speed)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG;);
    player_audio_render_config_t *cfg = (player_audio_render_config_t *)OBJ_GET_CFG(handle);
    if (cfg && cfg->stream_handle) {
        esp_audio_render_err_t render_ret = esp_audio_render_stream_set_speed(cfg->stream_handle, speed);
        if (render_ret != ESP_AUDIO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Set audio render stream speed error, ret: %d", render_ret);
            return ESP_GMF_ERR_FAIL;
        }
        return ESP_GMF_ERR_OK;
    }

    ESP_LOGI(TAG, "Audio render stream not initialized, line: %d", __LINE__);
    return ESP_GMF_ERR_INVALID_ARG;
}

esp_gmf_err_t player_audio_render_get_latency(esp_gmf_element_handle_t handle, uint32_t *latency)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG;);
    ESP_GMF_NULL_CHECK(TAG, latency, return ESP_GMF_ERR_INVALID_ARG;);
    player_audio_render_config_t *cfg = (player_audio_render_config_t *)OBJ_GET_CFG(handle);
    if (cfg == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    if (cfg && cfg->stream_handle) {
        *latency = 0;
        esp_audio_render_err_t render_ret = esp_audio_render_stream_get_latency(cfg->stream_handle, latency);
        if (render_ret != ESP_AUDIO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Get audio render stream latency error, ret: %d", render_ret);
            return ESP_GMF_ERR_FAIL;
        }
        return ESP_GMF_ERR_OK;
    }

    ESP_LOGI(TAG, "Audio render stream not initialized, line: %d", __LINE__);
    return ESP_GMF_ERR_INVALID_ARG;
}

esp_gmf_err_t player_audio_render_flush_enable(esp_gmf_element_handle_t handle, bool enable)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG;);
    player_audio_render_t *audio_render = (player_audio_render_t *)handle;
    player_audio_render_config_t *cfg = (player_audio_render_config_t *)OBJ_GET_CFG(handle);
    if (cfg == NULL || cfg->stream_handle == NULL) {
        ESP_LOGE(TAG, "Invalid render config");
        return ESP_GMF_ERR_INVALID_ARG;
    }
    /* Seek mode: drop frames while upstream is repositioning. When enabling,
     * also flush the underlying audio render stream to clear buffered PCM. */
    if (enable) {
        esp_audio_render_err_t ret = esp_audio_render_stream_flush(cfg->stream_handle);
        if (ret != ESP_AUDIO_RENDER_ERR_OK) {
            ESP_LOGW(TAG, "Flush audio render stream failed, ret=%d", ret);
            return ESP_GMF_ERR_FAIL;
        }
    }
    audio_render->is_seeking = enable;
    return ESP_GMF_ERR_OK;
}
