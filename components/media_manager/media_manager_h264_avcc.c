#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_video_dec.h"
#include "esp_video_dec_reg.h"
#include "esp_video_dec_h264.h"
#include "esp_h264_dec.h"
#include "esp_h264_dec_sw.h"

#define TAG "MEDIA_H264_AVCC"

typedef struct {
    esp_h264_dec_handle_t decoder;
    esp_h264_dec_param_sw_handle_t params;
    esp_h264_dec_out_frame_t decoded;
    uint8_t *annexb;
    const uint8_t *last_input;
    uint32_t last_consumed;
} media_h264_decoder_t;

static const esp_video_codec_pixel_fmt_t s_output_formats[] = {
    ESP_VIDEO_CODEC_PIXEL_FMT_YUV420P,
};

static esp_vc_err_t media_h264_get_caps(esp_video_dec_caps_t *caps)
{
    if (caps == NULL) {
        return ESP_VC_ERR_INVALID_ARG;
    }

    memset(caps, 0, sizeof(*caps));
    caps->in_frame_align = 1;
    caps->out_frame_align = 1;
    caps->out_fmts = s_output_formats;
    caps->out_fmt_num = sizeof(s_output_formats) / sizeof(s_output_formats[0]);
    return ESP_VC_ERR_OK;
}

static esp_vc_err_t media_h264_open(
    esp_video_dec_cfg_t *cfg,
    esp_video_dec_handle_t *handle)
{
    if (cfg == NULL || handle == NULL ||
        cfg->out_fmt != ESP_VIDEO_CODEC_PIXEL_FMT_YUV420P) {
        return ESP_VC_ERR_INVALID_ARG;
    }

    *handle = NULL;
    media_h264_decoder_t *decoder = calloc(1, sizeof(*decoder));
    if (decoder == NULL) {
        return ESP_VC_ERR_NO_MEMORY;
    }

    esp_h264_dec_cfg_sw_t h264_cfg = {
        .pic_type = ESP_H264_RAW_FMT_I420,
    };

    if (esp_h264_dec_sw_new(&h264_cfg, &decoder->decoder) != ESP_H264_ERR_OK ||
        decoder->decoder == NULL) {
        free(decoder);
        return ESP_VC_ERR_NO_MEMORY;
    }

    if (esp_h264_dec_open(decoder->decoder) != ESP_H264_ERR_OK ||
        esp_h264_dec_sw_get_param_hd(decoder->decoder, &decoder->params) != ESP_H264_ERR_OK) {
        esp_h264_dec_del(decoder->decoder);
        free(decoder);
        return ESP_VC_ERR_FAIL;
    }

    *handle = decoder;
    ESP_LOGI(TAG, "AVCC-aware TinyH264 decoder opened");
    return ESP_VC_ERR_OK;
}

/*
 * MP4 AVC samples use a big-endian NAL length followed by NAL bytes.
 * TinyH264 expects Annex-B start codes. Both prefixes are four bytes,
 * so conversion preserves every input offset and the consumed-byte count.
 */
static esp_vc_err_t media_h264_make_annexb(
    media_h264_decoder_t *decoder,
    const uint8_t *input,
    uint32_t input_size,
    const uint8_t **decode_data)
{
    if (input == NULL || input_size == 0 || decode_data == NULL) {
        return ESP_VC_ERR_INVALID_ARG;
    }

    if (input_size >= 4 &&
        input[0] == 0 && input[1] == 0 &&
        (input[2] == 1 || (input[2] == 0 && input[3] == 1))) {
        *decode_data = input;
        return ESP_VC_ERR_OK;
    }

    uint32_t offset = 0;
    uint32_t nal_count = 0;
    while (offset < input_size) {
        if (input_size - offset < 4) {
            return ESP_VC_ERR_WRONG_DATA;
        }

        uint32_t nal_size = ((uint32_t)input[offset] << 24) |
                            ((uint32_t)input[offset + 1] << 16) |
                            ((uint32_t)input[offset + 2] << 8) |
                            (uint32_t)input[offset + 3];
        if (nal_size == 0 || nal_size > input_size - offset - 4) {
            return ESP_VC_ERR_WRONG_DATA;
        }

        uint8_t nal_type = input[offset + 4] & 0x1F;
        if (nal_type == 0 || nal_type > 23) {
            return ESP_VC_ERR_WRONG_DATA;
        }

        offset += 4 + nal_size;
        nal_count++;
    }

    if (offset != input_size || nal_count == 0) {
        return ESP_VC_ERR_WRONG_DATA;
    }

    heap_caps_free(decoder->annexb);
    decoder->annexb = heap_caps_malloc(
        input_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (decoder->annexb == NULL) {
        decoder->annexb = heap_caps_malloc(input_size, MALLOC_CAP_8BIT);
    }
    if (decoder->annexb == NULL) {
        return ESP_VC_ERR_NO_MEMORY;
    }

    memcpy(decoder->annexb, input, input_size);
    offset = 0;
    while (offset < input_size) {
        uint32_t nal_size = ((uint32_t)input[offset] << 24) |
                            ((uint32_t)input[offset + 1] << 16) |
                            ((uint32_t)input[offset + 2] << 8) |
                            (uint32_t)input[offset + 3];

        decoder->annexb[offset] = 0;
        decoder->annexb[offset + 1] = 0;
        decoder->annexb[offset + 2] = 0;
        decoder->annexb[offset + 3] = 1;
        offset += 4 + nal_size;
    }

    *decode_data = decoder->annexb;
    ESP_LOGD(TAG, "Converted AVCC packet: %u bytes, %u NAL units",
             (unsigned)input_size, (unsigned)nal_count);
    return ESP_VC_ERR_OK;
}

static void media_h264_copy_output(
    media_h264_decoder_t *decoder,
    esp_video_dec_out_frame_t *output)
{
    memcpy(output->data, decoder->decoded.outbuf, decoder->decoded.out_size);
    output->pts = decoder->decoded.pts;
    output->dts = decoder->decoded.dts;
    output->decoded_size = decoder->decoded.out_size;
    switch (decoder->decoded.frame_type) {
    case ESP_H264_FRAME_TYPE_IDR:
        output->frame_type = ESP_VIDEO_CODEC_FRAME_TYPE_IDR;
        break;
    case ESP_H264_FRAME_TYPE_I:
        output->frame_type = ESP_VIDEO_CODEC_FRAME_TYPE_I;
        break;
    case ESP_H264_FRAME_TYPE_P:
        output->frame_type = ESP_VIDEO_CODEC_FRAME_TYPE_P;
        break;
    default:
        output->frame_type = ESP_VIDEO_CODEC_FRAME_TYPE_NONE;
        break;
    }
    decoder->decoded.out_size = 0;
}

static esp_vc_err_t media_h264_decode(
    esp_video_dec_handle_t handle,
    esp_video_dec_in_frame_t *input,
    esp_video_dec_out_frame_t *output)
{
    media_h264_decoder_t *decoder = handle;
    if (decoder == NULL || input == NULL || output == NULL ||
        (input->size > 0 && input->data == NULL)) {
        return ESP_VC_ERR_INVALID_ARG;
    }

    if (decoder->decoded.out_size > 0 &&
        decoder->last_input == input->data) {
        if (output->size < decoder->decoded.out_size) {
            return ESP_VC_ERR_BUF_NOT_ENOUGH;
        }

        media_h264_copy_output(decoder, output);
        input->consumed = decoder->last_consumed;
        decoder->last_input = NULL;
        heap_caps_free(decoder->annexb);
        decoder->annexb = NULL;
        return ESP_VC_ERR_OK;
    }

    if (input->size == 0) {
        output->decoded_size = 0;
        return ESP_VC_ERR_NO_MORE_DATA;
    }

    const uint8_t *decode_data = NULL;
    esp_vc_err_t ret = media_h264_make_annexb(
        decoder, input->data, input->size, &decode_data);
    if (ret != ESP_VC_ERR_OK) {
        ESP_LOGE(TAG, "Invalid H.264 packet framing (size=%u): %d",
                 (unsigned)input->size, (int)ret);
        heap_caps_free(decoder->annexb);
        decoder->annexb = NULL;
        return ret;
    }

    esp_h264_dec_in_frame_t h264_input = {
        .pts = input->pts,
        .dts = input->dts,
        .raw_data = {
            .buffer = (uint8_t *)decode_data,
            .len = input->size,
        },
    };

    memset(&decoder->decoded, 0, sizeof(decoder->decoded));
    ret = (esp_vc_err_t)esp_h264_dec_process(
        decoder->decoder, &h264_input, &decoder->decoded);
    if ((int)ret != (int)ESP_H264_ERR_OK) {
        ESP_LOGE(TAG, "TinyH264 decode failed: %d", (int)ret);
        heap_caps_free(decoder->annexb);
        decoder->annexb = NULL;
        return ESP_VC_ERR_FAIL;
    }

    input->consumed = h264_input.consume;
    if (decoder->decoded.out_size > 0) {
        if (output->size < decoder->decoded.out_size) {
            decoder->last_input = input->data;
            decoder->last_consumed = h264_input.consume;
            return ESP_VC_ERR_BUF_NOT_ENOUGH;
        }
        media_h264_copy_output(decoder, output);
    } else {
        output->pts = decoder->decoded.pts;
        output->decoded_size = 0;
    }

    heap_caps_free(decoder->annexb);
    decoder->annexb = NULL;
    return ESP_VC_ERR_OK;
}

static esp_vc_err_t media_h264_get_frame_info(
    esp_video_dec_handle_t handle,
    esp_video_codec_frame_info_t *info)
{
    media_h264_decoder_t *decoder = handle;
    if (decoder == NULL || info == NULL || decoder->params == NULL) {
        return ESP_VC_ERR_INVALID_ARG;
    }

    esp_h264_resolution_t resolution = {0};
    if (esp_h264_dec_get_resolution(decoder->params, &resolution) != ESP_H264_ERR_OK ||
        resolution.width == 0 || resolution.height == 0) {
        return ESP_VC_ERR_INVALID_STATE;
    }

    memset(info, 0, sizeof(*info));
    info->origin_res.width = resolution.width;
    info->origin_res.height = resolution.height;
    info->res = info->origin_res;
    return ESP_VC_ERR_OK;
}

static esp_vc_err_t media_h264_set(
    esp_video_dec_handle_t handle,
    esp_video_dec_set_type_t type,
    void *data,
    uint32_t size)
{
    (void)handle;
    (void)type;
    (void)data;
    (void)size;
    return ESP_VC_ERR_NOT_SUPPORTED;
}

static esp_vc_err_t media_h264_close(esp_video_dec_handle_t handle)
{
    media_h264_decoder_t *decoder = handle;
    if (decoder == NULL) {
        return ESP_VC_ERR_INVALID_ARG;
    }

    heap_caps_free(decoder->annexb);
    decoder->annexb = NULL;
    if (decoder->decoder != NULL) {
        esp_h264_dec_close(decoder->decoder);
        esp_h264_dec_del(decoder->decoder);
    }
    free(decoder);
    return ESP_VC_ERR_OK;
}

esp_vc_err_t media_manager_h264_avcc_register(void)
{
    esp_video_codec_desc_t desc = {
        .codec_type = ESP_VIDEO_CODEC_TYPE_H264,
        .is_hw = false,
        .codec_cc = ESP_VIDEO_DEC_SW_H264_TAG,
    };
    static const esp_video_dec_ops_t ops = {
        .get_caps = media_h264_get_caps,
        .open = media_h264_open,
        .decode = media_h264_decode,
        .get_frame_info = media_h264_get_frame_info,
        .set = media_h264_set,
        .close = media_h264_close,
    };
    return esp_video_dec_register(&desc, &ops);
}
