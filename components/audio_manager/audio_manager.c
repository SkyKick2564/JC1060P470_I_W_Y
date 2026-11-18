#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_board_manager.h"
#include "esp_board_manager_includes.h"
#include "esp_codec_dev.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"

#include "audio_manager.h"

#define AUDIO_MANAGER_TAG "AUDIO_MANAGER"
#define AUDIO_MANAGER_INPUT_BUFFER_SIZE 2048
#define AUDIO_MANAGER_OUTPUT_BUFFER_SIZE 8192
#define AUDIO_MANAGER_DEFAULT_VOLUME 50.0f

static dev_audio_codec_handles_t *s_audio_dac;
static bool s_initialized;
static float s_volume = AUDIO_MANAGER_DEFAULT_VOLUME;

bool audio_manager_is_initialized(void)
{
    return s_initialized;
}

esp_err_t audio_manager_init(void)
{
    esp_err_t err;

    if (s_initialized) {
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
        ESP_LOGE(AUDIO_MANAGER_TAG, "Get audio_dac handle failed: %s",
                 esp_err_to_name(err));
        s_audio_dac = NULL;
        return err;
    }

    if (s_audio_dac == NULL || s_audio_dac->codec_dev == NULL) {
        ESP_LOGE(AUDIO_MANAGER_TAG, "audio_dac codec handle is unavailable");
        s_audio_dac = NULL;
        return ESP_ERR_INVALID_STATE;
    }

    s_initialized = true;
    ESP_LOGI(AUDIO_MANAGER_TAG, "Initialized using Board Manager audio_dac");
    return ESP_OK;
}

esp_err_t audio_manager_set_volume(float volume)
{
    esp_err_t err;

    if (!s_initialized || s_audio_dac == NULL ||
        s_audio_dac->codec_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (volume < 0.0f || volume > 100.0f) {
        return ESP_ERR_INVALID_ARG;
    }

    err = esp_codec_dev_set_out_vol(s_audio_dac->codec_dev, volume);
    if (err != ESP_CODEC_DEV_OK) {
        ESP_LOGE(AUDIO_MANAGER_TAG, "Set output volume failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    s_volume = volume;
    return ESP_OK;
}

esp_err_t audio_manager_get_volume(float *volume)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (volume == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *volume = s_volume;
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

    esp_err_t err = esp_codec_dev_open(
        s_audio_dac->codec_dev,
        &sample_info
    );
    if (err != ESP_CODEC_DEV_OK) {
        ESP_LOGE(AUDIO_MANAGER_TAG, "Open audio codec failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    err = esp_codec_dev_set_out_vol(s_audio_dac->codec_dev, s_volume);
    if (err != ESP_CODEC_DEV_OK) {
        ESP_LOGE(AUDIO_MANAGER_TAG, "Set initial output volume failed: %s",
                 esp_err_to_name(err));
        esp_codec_dev_close(s_audio_dac->codec_dev);
        return err;
    }

    ESP_LOGI(AUDIO_MANAGER_TAG,
             "Codec opened: sample_rate=%" PRIu32 ", channels=%u, bits=%u",
             info->sample_rate,
             (unsigned)info->channel,
             (unsigned)info->bits_per_sample);
    return ESP_OK;
}

esp_err_t audio_manager_play_mp3(const char *path)
{
    FILE *file = NULL;
    uint8_t *input_buffer = NULL;
    uint8_t *output_buffer = NULL;
    size_t output_buffer_size = AUDIO_MANAGER_OUTPUT_BUFFER_SIZE;
    esp_audio_simple_dec_handle_t decoder = NULL;
    bool decoder_registered = false;
    bool simple_decoder_registered = false;
    bool codec_opened = false;
    bool codec_info_ready = false;
    esp_err_t result = ESP_OK;
    esp_audio_simple_dec_info_t decoder_info = {0};

    if (!s_initialized || s_audio_dac == NULL ||
        s_audio_dac->codec_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (path == NULL || path[0] == '\\0') {
        return ESP_ERR_INVALID_ARG;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGE(AUDIO_MANAGER_TAG, "Open MP3 file failed: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    do {
        input_buffer = malloc(AUDIO_MANAGER_INPUT_BUFFER_SIZE);
        output_buffer = malloc(output_buffer_size);
        if (input_buffer == NULL || output_buffer == NULL) {
            result = ESP_ERR_NO_MEM;
            break;
        }

        if (esp_audio_dec_register_default() != ESP_AUDIO_ERR_OK) {
            ESP_LOGE(AUDIO_MANAGER_TAG, "Register audio decoders failed");
            result = ESP_FAIL;
            break;
        }
        decoder_registered = true;

        if (esp_audio_simple_dec_register_default() != ESP_AUDIO_ERR_OK) {
            ESP_LOGE(AUDIO_MANAGER_TAG, "Register simple audio decoders failed");
            result = ESP_FAIL;
            break;
        }
        simple_decoder_registered = true;

        if (esp_audio_simple_check_audio_type(ESP_AUDIO_SIMPLE_DEC_TYPE_MP3) !=
            ESP_AUDIO_ERR_OK) {
            ESP_LOGE(AUDIO_MANAGER_TAG, "MP3 decoder is not enabled");
            result = ESP_ERR_NOT_SUPPORTED;
            break;
        }

        esp_audio_simple_dec_cfg_t decoder_config = {
            .dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
            .dec_cfg = NULL,
            .cfg_size = 0,
            .use_frame_dec = false,
        };

        if (esp_audio_simple_dec_open(&decoder_config, &decoder) !=
            ESP_AUDIO_ERR_OK) {
            ESP_LOGE(AUDIO_MANAGER_TAG, "Open MP3 decoder failed");
            result = ESP_FAIL;
            break;
        }

        ESP_LOGI(AUDIO_MANAGER_TAG, "Playing MP3: %s", path);

        while (result == ESP_OK) {
            size_t bytes_read = fread(
                input_buffer,
                1,
                AUDIO_MANAGER_INPUT_BUFFER_SIZE,
                file
            );

            if (bytes_read == 0) {
                if (ferror(file)) {
                    ESP_LOGE(AUDIO_MANAGER_TAG, "Read MP3 file failed");
                    result = ESP_FAIL;
                }
                break;
            }

            esp_audio_simple_dec_raw_t raw = {
                .buffer = input_buffer,
                .len = (uint32_t)bytes_read,
                .eos = bytes_read < AUDIO_MANAGER_INPUT_BUFFER_SIZE,
            };

            while (raw.len > 0) {
                esp_audio_simple_dec_out_t decoded = {
                    .buffer = output_buffer,
                    .len = (uint32_t)output_buffer_size,
                };

                esp_audio_err_t decode_err = esp_audio_simple_dec_process(
                    decoder,
                    &raw,
                    &decoded
                );

                if (decode_err == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                    uint8_t *new_buffer = realloc(
                        output_buffer,
                        decoded.needed_size
                    );
                    if (new_buffer == NULL) {
                        result = ESP_ERR_NO_MEM;
                        break;
                    }

                    output_buffer = new_buffer;
                    output_buffer_size = decoded.needed_size;
                    continue;
                }

                if (decode_err != ESP_AUDIO_ERR_OK) {
                    ESP_LOGE(AUDIO_MANAGER_TAG, "MP3 decode failed: %d",
                             (int)decode_err);
                    result = ESP_FAIL;
                    break;
                }

                if (decoded.decoded_size > 0) {
                    if (!codec_info_ready) {
                        if (esp_audio_simple_dec_get_info(
                                decoder, &decoder_info) != ESP_AUDIO_ERR_OK) {
                            ESP_LOGE(AUDIO_MANAGER_TAG,
                                     "Get decoded MP3 format failed");
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

                    esp_err_t write_err = esp_codec_dev_write(
                        s_audio_dac->codec_dev,
                        decoded.buffer,
                        decoded.decoded_size
                    );
                    if (write_err != ESP_CODEC_DEV_OK) {
                        ESP_LOGE(AUDIO_MANAGER_TAG,
                                 "Write PCM to codec failed: %s",
                                 esp_err_to_name(write_err));
                        result = write_err;
                        break;
                    }
                }

                if (raw.consumed > raw.len) {
                    ESP_LOGE(AUDIO_MANAGER_TAG,
                             "MP3 decoder returned invalid consumed size");
                    result = ESP_FAIL;
                    break;
                }

                raw.len -= raw.consumed;
                raw.buffer += raw.consumed;

                if (raw.len > 0 && raw.consumed == 0 &&
                    decoded.decoded_size == 0) {
                    ESP_LOGE(AUDIO_MANAGER_TAG, "MP3 decoder made no progress");
                    result = ESP_FAIL;
                    break;
                }
            }

            if (result != ESP_OK || raw.eos) {
                break;
            }
        }

        if (result == ESP_OK && !codec_info_ready) {
            ESP_LOGE(AUDIO_MANAGER_TAG, "MP3 file contains no decodable audio");
            result = ESP_FAIL;
        }
    } while (0);

    if (result == ESP_OK) {
        ESP_LOGI(AUDIO_MANAGER_TAG, "MP3 playback finished");
    }

    if (codec_opened) {
        esp_codec_dev_close(s_audio_dac->codec_dev);
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

    free(input_buffer);
    free(output_buffer);

    return result;
}
