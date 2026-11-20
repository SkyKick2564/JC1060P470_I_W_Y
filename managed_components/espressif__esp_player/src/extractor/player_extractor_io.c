/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_gmf_io.h"

#include "player_stream.h"
#include "player_extractor_io.h"
#include "player_extractor.h"

/* Upper bound when byte rate is unknown; per-read sleep is capped to one RAW chunk duration. */
#define PLAYER_RAW_READ_AHEAD_PACE_MAX_MS  (50U)

/* Must match PLAYER_RAW_MAX_FRAME_SIZE in player_extractor.c */
#define PLAYER_RAW_PACE_CHUNK_BYTES  (2048U)

typedef struct {
    int64_t window_start_us;
    uint32_t calls, errors;
    uint64_t total_us, pace_us, io_us, bytes, max_io_us;
} player_extractor_profile_t;
static player_extractor_profile_t s_extractor_profile;

static void player_extractor_profile_add(uint64_t total_us, uint64_t pace_us,
                                         uint64_t io_us, uint32_t bytes, bool error)
{
    int64_t now_us = esp_timer_get_time();
    player_extractor_profile_t *p = &s_extractor_profile;
    if (p->window_start_us == 0) p->window_start_us = now_us;
    p->calls++;
    p->total_us += total_us;
    p->pace_us += pace_us;
    p->io_us += io_us;
    p->bytes += bytes;
    p->errors += error ? 1U : 0U;
    if (io_us > p->max_io_us) p->max_io_us = io_us;
    uint64_t elapsed_us = (uint64_t)(now_us - p->window_start_us);
    if (elapsed_us < 1000000ULL) return;
    ESP_LOGI(ESP_PLAYER_TAG, "PROFILE extractor: reads/s=%llu bytes/s=%llu errors=%u avg_total=%lluus avg_pace=%lluus avg_io=%lluus max_io=%lluus",
             (unsigned long long)((uint64_t)p->calls * 1000000ULL / elapsed_us),
             (unsigned long long)(p->bytes * 1000000ULL / elapsed_us), (unsigned)p->errors,
             (unsigned long long)(p->calls ? p->total_us / p->calls : 0),
             (unsigned long long)(p->calls ? p->pace_us / p->calls : 0),
             (unsigned long long)(p->calls ? p->io_us / p->calls : 0),
             (unsigned long long)p->max_io_us);
    p->window_start_us = now_us;
    p->calls = p->errors = 0;
    p->total_us = p->pace_us = p->io_us = p->bytes = p->max_io_us = 0;
}

#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
static uint32_t player_raw_pcm_byte_rate(const esp_player_stream_t *stream)
{
    if (stream == NULL || stream->audio_side == NULL) {
        return 0;
    }
    const esp_player_audio_stream_info_t *ai = &stream->audio_side->track_info.audio_info;
    if (ai->sample_rate == 0 || ai->channels == 0 || ai->bits_per_sample == 0) {
        return 0;
    }
    return ai->sample_rate * (uint32_t)ai->channels * ((uint32_t)ai->bits_per_sample / 8U);
}

static uint32_t player_raw_read_pace_cap_ms(const esp_player_stream_t *stream)
{
    uint32_t byte_rate = player_raw_pcm_byte_rate(stream);
    if (byte_rate == 0) {
        return PLAYER_RAW_READ_AHEAD_PACE_MAX_MS;
    }
    uint64_t chunk_ms = ((uint64_t)PLAYER_RAW_PACE_CHUNK_BYTES * 1000ULL) / byte_rate;
    if (chunk_ms == 0) {
        chunk_ms = 1;
    }
    if (chunk_ms > PLAYER_RAW_READ_AHEAD_PACE_MAX_MS) {
        return PLAYER_RAW_READ_AHEAD_PACE_MAX_MS;
    }
    return (uint32_t)chunk_ms;
}

/* Lead time allowed before throttling; render batches PCM so decode PTS tracks supply better than render PTS. */
static uint32_t player_raw_read_ahead_target_ms(const esp_player_stream_t *stream)
{
    uint32_t target_ms = ESP_PLAYER_AUDIO_RENDER_FRAME_MS;
    uint32_t chunk_ms = player_raw_read_pace_cap_ms(stream);
    if (chunk_ms * 2U > target_ms) {
        target_ms = chunk_ms * 2U;
    }
    return target_ms;
}
#else
static uint32_t player_raw_read_pace_cap_ms(const esp_player_stream_t *stream)
{
    (void)stream;
    return PLAYER_RAW_READ_AHEAD_PACE_MAX_MS;
}
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */

static void player_extractor_raw_read_pace(esp_player_stream_t *stream, esp_gmf_element_handle_t ext_el,
                                           uint64_t aud_pts)
{
    if (stream == NULL || ext_el == NULL || !player_extractor_is_raw_source(ext_el)) {
        return;
    }
    uint64_t ext_pts = 0;
    if (player_extractor_get_last_pts(ext_el, &ext_pts) != ESP_GMF_ERR_OK) {
        return;
    }
    int64_t ahead_ms = (int64_t)ext_pts - (int64_t)aud_pts;
#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
    uint32_t target_ms = player_raw_read_ahead_target_ms(stream);
    if (ahead_ms <= (int64_t)target_ms) {
        return;
    }
    ahead_ms -= (int64_t)target_ms;
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */
    if (ahead_ms <= 0) {
        return;
    }
    uint32_t pace_cap_ms = player_raw_read_pace_cap_ms(stream);
    uint32_t sleep_ms = (ahead_ms > (int64_t)pace_cap_ms) ? pace_cap_ms : (uint32_t)ahead_ms;
    vTaskDelay(pdMS_TO_TICKS(sleep_ms));
}

static esp_player_err_t player_ensure_input_open(esp_player_stream_t *stream)
{
    if (stream->input_state == ESP_PLAYER_INPUT_OPENED) {
        return ESP_PLAYER_ERR_OK;
    }
    if (stream->input_state == ESP_PLAYER_INPUT_OPEN_FAILED) {
        return ESP_PLAYER_ERR_FAIL;
    }
    if (stream->_is_stop) {
        return ESP_PLAYER_ERR_FAIL;
    }
    if (esp_gmf_io_open(stream->input_handle) != ESP_GMF_ERR_OK) {
        ESP_LOGE(ESP_PLAYER_TAG, "Failed to open input handle");
        stream->input_state = ESP_PLAYER_INPUT_OPEN_FAILED;
        return ESP_PLAYER_ERR_FAIL;
    }
    stream->input_state = ESP_PLAYER_INPUT_OPENED;
    return ESP_PLAYER_ERR_OK;
}

int _extractor_read(void *buffer, uint32_t size, void *ctx)
{
    int64_t profile_start_us = esp_timer_get_time();
    uint64_t pace_us = 0, io_us = 0;
    esp_player_stream_t *stream = (esp_player_stream_t *)ctx;
    if (player_ensure_input_open(stream) != ESP_PLAYER_ERR_OK) {
        player_extractor_profile_add((uint64_t)(esp_timer_get_time() - profile_start_us), 0, 0, 0, true);
        return -1;
    }
    esp_gmf_payload_t load = {0};
    uint32_t pos = 0;
    uint64_t pace_pts = 0;
    uint64_t render_pts = 0;
    uint64_t ext_pts = 0;
    esp_gmf_element_handle_t ext_el = player_extractor_el(stream);
    if (stream->sync_handle != NULL) {
        /* RAW pace: decode PTS tracks extractor chunk rate; render PTS batches PCM. */
        pace_pts = player_sync_get_audio_decode_pts_ms(stream->sync_handle);
        int64_t pace_start_us = esp_timer_get_time();
        player_extractor_raw_read_pace(stream, ext_el, pace_pts);
        pace_us += (uint64_t)(esp_timer_get_time() - pace_start_us);
        render_pts = player_sync_get_audio_render_pts_ms(stream->sync_handle);
    }
    if (ext_el) {
        player_extractor_get_last_pts(ext_el, &ext_pts);
    }
    int64_t diff_pts = (int64_t)render_pts - (int64_t)ext_pts;
    diff_pts = diff_pts > 0 ? diff_pts : ESP_PLAYER_READ_WAIT_TIME_MS;
_read_again:
    load.buf = buffer + pos;
    load.buf_length = size - pos;
    load.valid_size = 0;
    int64_t io_start_us = esp_timer_get_time();
    esp_gmf_err_io_t ret = esp_gmf_io_acquire_read(stream->input_handle, &load, load.buf_length, pdMS_TO_TICKS(diff_pts));
    io_us += (uint64_t)(esp_timer_get_time() - io_start_us);
    if (ret == ESP_GMF_IO_TIMEOUT) {
        if ((stream->_is_stop == true) && pos == 0) {
            player_extractor_profile_add((uint64_t)(esp_timer_get_time() - profile_start_us), pace_us, io_us, 0, false);
            return 0;
        }
        if (pos > 0) {
            goto _read_done;
        }
    }
    if ((ret == ESP_GMF_IO_FAIL || ret == ESP_GMF_IO_ABORT) && pos > 0) {
        goto _read_done;
    }
    if (ret == ESP_GMF_IO_OK && load.valid_size > 0 && load.buf != NULL) {
        if (load.buf != (uint8_t *)buffer + pos) {
            memcpy((uint8_t *)buffer + pos, load.buf, load.valid_size);
        }
    }
    io_start_us = esp_timer_get_time();
    esp_gmf_io_release_read(stream->input_handle, &load, pdMS_TO_TICKS(ESP_PLAYER_READ_WAIT_TIME_MS));
    io_us += (uint64_t)(esp_timer_get_time() - io_start_us);
    pos += load.valid_size;
    if (pos < size && load.is_done != true) {
        goto _read_again;
    }
_read_done:
    load.valid_size = pos;

    int result = (pos > 0) ? (int)pos :
                 (ret == ESP_GMF_IO_OK ? (int)load.valid_size : -1);
    player_extractor_profile_add((uint64_t)(esp_timer_get_time() - profile_start_us),
                                 pace_us, io_us, (uint32_t)(result > 0 ? result : 0), result < 0);
    return result;
}

int _extractor_seek(uint32_t position, void *ctx)
{
    esp_player_stream_t *stream = (esp_player_stream_t *)ctx;
    if (player_ensure_input_open(stream) != ESP_PLAYER_ERR_OK) {
        return -1;
    }
    esp_gmf_err_t ret = esp_gmf_io_seek(stream->input_handle, position);
    return ret == ESP_GMF_ERR_OK ? 0 : -1;
}

uint32_t _extractor_total_size(void *ctx)
{
    esp_player_stream_t *stream = (esp_player_stream_t *)ctx;
    uint64_t total_size = 0;
    if (player_ensure_input_open(stream) != ESP_PLAYER_ERR_OK) {
        return 0x7FFFFFFF;
    }
    esp_gmf_io_get_size(stream->input_handle, &total_size);
    if (total_size == 0) {
        // For live streams (e.g. HLS), file size is unknown (0). Return a large sentinel so the extractor does not treat every frame as a truncated-file EOS.
        return 0x7FFFFFFF;
    }
    if (total_size > UINT32_MAX) {
        return UINT32_MAX;
    }
    return (uint32_t)total_size;
}
