#include "media_manager_h264_openh264.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_video_dec.h"
#include "esp_video_dec_reg.h"
#include "esp_video_dec_h264.h"

#include "codec_api.h"
#include "codec_app_def.h"

#define TAG "MEDIA_H264_OPENH264"

typedef struct {
    ISVCDecoder *decoder;
    uint8_t *annexb;
    uint8_t *pending_frame;
    uint32_t pending_size;
    uint32_t pending_consumed;
    uint32_t width;
    uint32_t height;
    esp_video_codec_frame_type_t frame_type;
    uint32_t pending_pts;
    uint32_t pending_dts;
    uint32_t timing_access_units;
    uint64_t timing_decode_us;
    uint32_t timing_decode_max_us;
    uint64_t timing_codec_us;
    uint64_t timing_store_us;
    uint32_t timing_codec_max_us;
    uint32_t timing_store_max_us;
} media_openh264_t;

static uint8_t *s_parameter_sets;
static size_t s_parameter_sets_size;

static const esp_video_codec_pixel_fmt_t s_output_formats[] = {
    ESP_VIDEO_CODEC_PIXEL_FMT_YUV420P,
};

static void *media_openh264_alloc(size_t size)
{
    void *ptr = heap_caps_aligned_alloc(
        64, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == NULL) {
        ptr = heap_caps_aligned_alloc(64, size, MALLOC_CAP_8BIT);
    }
    return ptr;
}

esp_vc_err_t media_manager_h264_openh264_set_avcc(const uint8_t *record, size_t size)
{
    if (record == NULL || size < 7 || record[0] != 1) {
        return ESP_VC_ERR_INVALID_ARG;
    }

    size_t offset = 6;
    uint8_t sps_count = record[5] & 0x1F;
    size_t output_size = 0;

    for (uint8_t i = 0; i < sps_count; i++) {
        if (offset + 2 > size) {
            return ESP_VC_ERR_WRONG_DATA;
        }
        uint16_t length = ((uint16_t)record[offset] << 8) | record[offset + 1];
        offset += 2;
        if (length == 0 || offset + length > size) {
            return ESP_VC_ERR_WRONG_DATA;
        }
        output_size += 4 + length;
        offset += length;
    }

    if (offset >= size) {
        return ESP_VC_ERR_WRONG_DATA;
    }
    uint8_t pps_count = record[offset++];
    for (uint8_t i = 0; i < pps_count; i++) {
        if (offset + 2 > size) {
            return ESP_VC_ERR_WRONG_DATA;
        }
        uint16_t length = ((uint16_t)record[offset] << 8) | record[offset + 1];
        offset += 2;
        if (length == 0 || offset + length > size) {
            return ESP_VC_ERR_WRONG_DATA;
        }
        output_size += 4 + length;
        offset += length;
    }

    if (sps_count == 0 || pps_count == 0 || output_size == 0) {
        return ESP_VC_ERR_WRONG_DATA;
    }

    uint8_t *annexb = (uint8_t *)media_openh264_alloc(output_size);
    if (annexb == NULL) {
        return ESP_VC_ERR_NO_MEMORY;
    }

    offset = 6;
    size_t dst = 0;
    for (uint8_t i = 0; i < (uint8_t)(sps_count + pps_count); i++) {
        if (i == sps_count) {
            offset++;
        }
        uint16_t length = ((uint16_t)record[offset] << 8) | record[offset + 1];
        offset += 2;
        annexb[dst++] = 0;
        annexb[dst++] = 0;
        annexb[dst++] = 0;
        annexb[dst++] = 1;
        memcpy(annexb + dst, record + offset, length);
        dst += length;
        offset += length;
    }

    heap_caps_free(s_parameter_sets);
    s_parameter_sets = annexb;
    s_parameter_sets_size = output_size;
    ESP_LOGI(TAG, "Stored avcC SPS/PPS: %u bytes", (unsigned)output_size);
    return ESP_VC_ERR_OK;
}

static esp_vc_err_t media_openh264_get_caps(esp_video_dec_caps_t *caps)
{
    if (caps == NULL) {
        return ESP_VC_ERR_INVALID_ARG;
    }

    memset(caps, 0, sizeof(*caps));
    /*
     * Compressed MP4 access units arrive in extractor-owned buffers whose
     * alignment is not guaranteed. OpenH264 copies each NAL into its own
     * 64-byte-aligned Annex-B buffer before decoding, so input alignment
     * must remain 1. Decoded YUV output is separately allocated at 64-byte
     * alignment for the PPA/cache-maintenance path.
     */
    caps->in_frame_align = 1;
    caps->out_frame_align = 64;
    caps->out_fmts = s_output_formats;
    caps->out_fmt_num = sizeof(s_output_formats) / sizeof(s_output_formats[0]);
    return ESP_VC_ERR_OK;
}

static void media_openh264_feed_parameter_sets(media_openh264_t *ctx)
{
    if (s_parameter_sets == NULL || s_parameter_sets_size == 0) {
        ESP_LOGW(TAG, "No avcC SPS/PPS supplied; decoder needs in-band parameter sets");
        return;
    }

    size_t offset = 0;
    while (offset + 4 < s_parameter_sets_size) {
        size_t nal_start = offset + 4;
        size_t nal_end = nal_start;
        while (nal_end + 3 < s_parameter_sets_size &&
               !(s_parameter_sets[nal_end] == 0 &&
                 s_parameter_sets[nal_end + 1] == 0 &&
                 s_parameter_sets[nal_end + 2] == 0 &&
                 s_parameter_sets[nal_end + 3] == 1)) {
            nal_end++;
        }
        if (nal_end + 3 >= s_parameter_sets_size) {
            nal_end = s_parameter_sets_size;
        }

        uint8_t *planes[3] = { NULL, NULL, NULL };
        SBufferInfo info = {};
        (void)ctx->decoder->DecodeFrameNoDelay(
            s_parameter_sets + offset, (int)(nal_end - offset), planes, &info);
        offset = nal_end;
    }
}

static esp_vc_err_t media_openh264_open(
    esp_video_dec_cfg_t *cfg,
    esp_video_dec_handle_t *handle)
{
    if (cfg == NULL || handle == NULL ||
        cfg->out_fmt != ESP_VIDEO_CODEC_PIXEL_FMT_YUV420P) {
        return ESP_VC_ERR_INVALID_ARG;
    }

    *handle = NULL;
    media_openh264_t *ctx = (media_openh264_t *)calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_VC_ERR_NO_MEMORY;
    }

    if (WelsCreateDecoder(&ctx->decoder) != 0 || ctx->decoder == NULL) {
        free(ctx);
        return ESP_VC_ERR_NO_MEMORY;
    }

    SDecodingParam param = {};
    param.uiTargetDqLayer = (unsigned char)-1;
    /* SD-card playback uses a local, complete bitstream; skip concealment work. */
    param.eEcActiveIdc = ERROR_CON_DISABLE;
    param.sVideoProperty.size = sizeof(param.sVideoProperty);
    param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;

    if (ctx->decoder->Initialize(&param) != 0) {
        WelsDestroyDecoder(ctx->decoder);
        free(ctx);
        return ESP_VC_ERR_FAIL;
    }

    media_openh264_feed_parameter_sets(ctx);
    *handle = ctx;
    ESP_LOGI(TAG, "OpenH264 Main/High-capable software decoder opened");
    return ESP_VC_ERR_OK;
}

static esp_vc_err_t media_openh264_store_frame(
    media_openh264_t *ctx,
    uint8_t **planes,
    const SBufferInfo *info,
    uint32_t pts,
    uint32_t dts,
    uint8_t nal_type,
    uint32_t consumed)
{
    if (info->iBufferStatus != 1 || planes[0] == NULL ||
        planes[1] == NULL || planes[2] == NULL) {
        return ESP_VC_ERR_NO_MORE_DATA;
    }

    const SSysMEMBuffer *sys = &info->UsrData.sSystemBuffer;
    uint32_t width = (uint32_t)sys->iWidth;
    uint32_t height = (uint32_t)sys->iHeight;
    if (width == 0 || height == 0 || sys->iStride[0] < (int)width ||
        sys->iStride[1] < (int)((width + 1) / 2)) {
        return ESP_VC_ERR_WRONG_DATA;
    }

    uint32_t chroma_width = (width + 1) / 2;
    uint32_t chroma_height = (height + 1) / 2;
    uint32_t required = width * height + 2 * chroma_width * chroma_height;
    uint8_t *frame = (uint8_t *)media_openh264_alloc(required);
    if (frame == NULL) {
        return ESP_VC_ERR_NO_MEMORY;
    }

    uint8_t *dst_y = frame;
    uint8_t *dst_u = dst_y + width * height;
    uint8_t *dst_v = dst_u + chroma_width * chroma_height;
    for (uint32_t row = 0; row < height; row++) {
        memcpy(dst_y + row * width, planes[0] + row * sys->iStride[0], width);
    }
    for (uint32_t row = 0; row < chroma_height; row++) {
        memcpy(dst_u + row * chroma_width,
               planes[1] + row * sys->iStride[1], chroma_width);
        memcpy(dst_v + row * chroma_width,
               planes[2] + row * sys->iStride[1], chroma_width);
    }

    heap_caps_free(ctx->pending_frame);
    ctx->pending_frame = frame;
    ctx->pending_size = required;
    ctx->pending_consumed = consumed;
    ctx->width = width;
    ctx->height = height;
    ctx->pending_pts = pts;
    ctx->pending_dts = dts;
    ctx->frame_type = (nal_type == 5) ? ESP_VIDEO_CODEC_FRAME_TYPE_IDR :
                      (nal_type == 1) ? ESP_VIDEO_CODEC_FRAME_TYPE_P :
                                        ESP_VIDEO_CODEC_FRAME_TYPE_NONE;
    return ESP_VC_ERR_OK;
}

static esp_vc_err_t media_openh264_copy_pending(
    media_openh264_t *ctx,
    esp_video_dec_in_frame_t *input,
    esp_video_dec_out_frame_t *output)
{
    if (ctx->pending_frame == NULL || ctx->pending_size == 0) {
        return ESP_VC_ERR_NO_MORE_DATA;
    }
    if (output->size < ctx->pending_size) {
        return ESP_VC_ERR_BUF_NOT_ENOUGH;
    }

    memcpy(output->data, ctx->pending_frame, ctx->pending_size);
    output->decoded_size = ctx->pending_size;
    output->pts = ctx->pending_pts;
    output->dts = ctx->pending_dts;
    output->frame_type = ctx->frame_type;
    input->consumed = ctx->pending_consumed;
    heap_caps_free(ctx->pending_frame);
    ctx->pending_frame = NULL;
    ctx->pending_size = 0;
    return ESP_VC_ERR_OK;
}

static esp_vc_err_t media_openh264_decode(
    esp_video_dec_handle_t handle,
    esp_video_dec_in_frame_t *input,
    esp_video_dec_out_frame_t *output)
{
    media_openh264_t *ctx = (media_openh264_t *)handle;
    if (ctx == NULL || input == NULL || output == NULL ||
        (input->size > 0 && input->data == NULL) || output->data == NULL) {
        return ESP_VC_ERR_INVALID_ARG;
    }

    if (ctx->pending_size > 0) {
        return media_openh264_copy_pending(ctx, input, output);
    }

    if (input->size == 0) {
        uint8_t *planes[3] = { NULL, NULL, NULL };
        SBufferInfo info = {};
        (void)ctx->decoder->FlushFrame(planes, &info);
        if (info.iBufferStatus != 1) {
            output->decoded_size = 0;
            return ESP_VC_ERR_NO_MORE_DATA;
        }
        esp_vc_err_t store_ret = media_openh264_store_frame(
            ctx, planes, &info, input->pts, input->dts, 0, 0);
        if (store_ret != ESP_VC_ERR_OK) {
            return store_ret;
        }
        return media_openh264_copy_pending(ctx, input, output);
    }

    const int64_t decode_start_us = esp_timer_get_time();
    uint8_t *planes[3] = { NULL, NULL, NULL };
    SBufferInfo info = {};
    uint8_t frame_type_nal = 0;
    uint32_t nal_count = 0;
    size_t annexb_size = 0;

    /*
     * Decode a complete access unit in one OpenH264 call. The previous
     * implementation called DecodeFrameNoDelay once for every NAL, causing
     * repeated decoder entry/bitstream setup for SEI, SPS/PPS and each slice.
     * MP4 AVCC length fields are replaced with Annex-B start codes; the total
     * byte count is unchanged for four-byte NAL lengths.
     */
    heap_caps_free(ctx->annexb);
    ctx->annexb = (uint8_t *)media_openh264_alloc((size_t)input->size * 2U + 4U);
    if (ctx->annexb == NULL) {
        return ESP_VC_ERR_NO_MEMORY;
    }

    const bool input_is_annexb =
        input->size >= 4 &&
        input->data[0] == 0 && input->data[1] == 0 &&
        (input->data[2] == 1 ||
         (input->data[2] == 0 && input->data[3] == 1));

    if (input_is_annexb) {
        uint32_t src_offset = 0;
        while (src_offset < input->size) {
            uint32_t prefix_size = 0;
            if (input->size - src_offset >= 4 &&
                input->data[src_offset] == 0 && input->data[src_offset + 1] == 0 &&
                input->data[src_offset + 2] == 0 && input->data[src_offset + 3] == 1) {
                prefix_size = 4;
            } else if (input->size - src_offset >= 3 &&
                       input->data[src_offset] == 0 && input->data[src_offset + 1] == 0 &&
                       input->data[src_offset + 2] == 1) {
                prefix_size = 3;
            } else {
                heap_caps_free(ctx->annexb);
                ctx->annexb = NULL;
                return ESP_VC_ERR_WRONG_DATA;
            }

            uint32_t nal_start = src_offset + prefix_size;
            uint32_t nal_end = nal_start;
            while (nal_end + 3 < input->size &&
                   !(input->data[nal_end] == 0 && input->data[nal_end + 1] == 0 &&
                     (input->data[nal_end + 2] == 1 ||
                      (input->data[nal_end + 2] == 0 && input->data[nal_end + 3] == 1)))) {
                nal_end++;
            }
            if (nal_end + 3 >= input->size) {
                nal_end = input->size;
            }
            if (nal_end <= nal_start) {
                heap_caps_free(ctx->annexb);
                ctx->annexb = NULL;
                return ESP_VC_ERR_WRONG_DATA;
            }

            uint32_t nal_size = nal_end - nal_start;
            ctx->annexb[annexb_size++] = 0;
            ctx->annexb[annexb_size++] = 0;
            ctx->annexb[annexb_size++] = 0;
            ctx->annexb[annexb_size++] = 1;
            memcpy(ctx->annexb + annexb_size, input->data + nal_start, nal_size);
            annexb_size += nal_size;

            uint8_t nal_type = input->data[nal_start] & 0x1F;
            if (nal_type == 5) {
                frame_type_nal = 5;
            } else if (nal_type == 1 && frame_type_nal == 0) {
                frame_type_nal = 1;
            }
            nal_count++;
            src_offset = nal_end;
        }
    } else {
        uint32_t src_offset = 0;
        while (src_offset < input->size) {
            if (input->size - src_offset < 4) {
                heap_caps_free(ctx->annexb);
                ctx->annexb = NULL;
                return ESP_VC_ERR_WRONG_DATA;
            }

            uint32_t nal_size = ((uint32_t)input->data[src_offset] << 24) |
                                ((uint32_t)input->data[src_offset + 1] << 16) |
                                ((uint32_t)input->data[src_offset + 2] << 8) |
                                input->data[src_offset + 3];
            src_offset += 4;
            if (nal_size == 0 || nal_size > input->size - src_offset ||
                annexb_size + 4U + nal_size > (size_t)input->size * 2U + 4U) {
                heap_caps_free(ctx->annexb);
                ctx->annexb = NULL;
                return ESP_VC_ERR_WRONG_DATA;
            }

            ctx->annexb[annexb_size++] = 0;
            ctx->annexb[annexb_size++] = 0;
            ctx->annexb[annexb_size++] = 0;
            ctx->annexb[annexb_size++] = 1;
            memcpy(ctx->annexb + annexb_size, input->data + src_offset, nal_size);
            uint8_t nal_type = input->data[src_offset] & 0x1F;
            if (nal_type == 5) {
                frame_type_nal = 5;
            } else if (nal_type == 1 && frame_type_nal == 0) {
                frame_type_nal = 1;
            }
            annexb_size += nal_size;
            src_offset += nal_size;
            nal_count++;
        }
    }

    if (nal_count == 0 || annexb_size == 0 || annexb_size > ((size_t)input->size * 2U + 4U)) {
        heap_caps_free(ctx->annexb);
        ctx->annexb = NULL;
        return ESP_VC_ERR_WRONG_DATA;
    }

    (void)ctx->decoder->DecodeFrameNoDelay(
        ctx->annexb, (int)annexb_size, planes, &info);

    heap_caps_free(ctx->annexb);
    ctx->annexb = NULL;

    input->consumed = input->size;
    const uint32_t decode_elapsed_us =
        (uint32_t)(esp_timer_get_time() - decode_start_us);

    ctx->timing_access_units++;
    ctx->timing_decode_us += decode_elapsed_us;
    ctx->timing_codec_us += decode_elapsed_us;
    if (decode_elapsed_us > ctx->timing_codec_max_us) {
        ctx->timing_codec_max_us = decode_elapsed_us;
    }
    if (decode_elapsed_us > ctx->timing_decode_max_us) {
        ctx->timing_decode_max_us = decode_elapsed_us;
    }

    if ((ctx->timing_access_units % 25U) == 0U) {
        ESP_LOGI(TAG,
                 "25 AU timing avg(us): total=%u codec=%u store=%u | max(us): total=%u codec=%u store=%u",
                 (unsigned)(ctx->timing_decode_us / 25U),
                 (unsigned)(ctx->timing_codec_us / 25U),
                 (unsigned)(ctx->timing_store_us / 25U),
                 (unsigned)ctx->timing_decode_max_us,
                 (unsigned)ctx->timing_codec_max_us,
                 (unsigned)ctx->timing_store_max_us);
        ctx->timing_decode_us = 0;
        ctx->timing_decode_max_us = 0;
        ctx->timing_codec_us = 0;
        ctx->timing_codec_max_us = 0;
        ctx->timing_store_us = 0;
        ctx->timing_store_max_us = 0;
    }

    if (info.iBufferStatus == 1) {
        int64_t store_start_us = esp_timer_get_time();
        esp_vc_err_t store_ret = media_openh264_store_frame(
            ctx, planes, &info, input->pts, input->dts,
            frame_type_nal, input->size);
        uint32_t store_elapsed_us =
            (uint32_t)(esp_timer_get_time() - store_start_us);
        ctx->timing_store_us += store_elapsed_us;
        if (store_elapsed_us > ctx->timing_store_max_us) {
            ctx->timing_store_max_us = store_elapsed_us;
        }
        if (store_ret != ESP_VC_ERR_OK) {
            return store_ret;
        }
        return media_openh264_copy_pending(ctx, input, output);
    }

    output->decoded_size = 0;
    output->pts = input->pts;
    output->dts = input->dts;
    return ESP_VC_ERR_OK;
}

static esp_vc_err_t media_openh264_get_frame_info(
    esp_video_dec_handle_t handle,
    esp_video_codec_frame_info_t *info)
{
    media_openh264_t *ctx = (media_openh264_t *)handle;
    if (ctx == NULL || info == NULL || ctx->width == 0 || ctx->height == 0) {
        return ESP_VC_ERR_INVALID_STATE;
    }

    memset(info, 0, sizeof(*info));
    info->origin_res.width = ctx->width;
    info->origin_res.height = ctx->height;
    info->res = info->origin_res;
    return ESP_VC_ERR_OK;
}

static esp_vc_err_t media_openh264_set(
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

static esp_vc_err_t media_openh264_close(esp_video_dec_handle_t handle)
{
    media_openh264_t *ctx = (media_openh264_t *)handle;
    if (ctx == NULL) {
        return ESP_VC_ERR_INVALID_ARG;
    }

    heap_caps_free(ctx->annexb);
    heap_caps_free(ctx->pending_frame);
    if (ctx->decoder != NULL) {
        ctx->decoder->Uninitialize();
        WelsDestroyDecoder(ctx->decoder);
    }
    free(ctx);
    return ESP_VC_ERR_OK;
}

esp_vc_err_t media_manager_h264_openh264_register(void)
{
    esp_video_codec_desc_t desc = {
        .codec_type = ESP_VIDEO_CODEC_TYPE_H264,
        .is_hw = false,
        .codec_cc = ESP_VIDEO_DEC_SW_H264_TAG,
    };
    static const esp_video_dec_ops_t ops = {
        .get_caps = media_openh264_get_caps,
        .open = media_openh264_open,
        .decode = media_openh264_decode,
        .get_frame_info = media_openh264_get_frame_info,
        .set = media_openh264_set,
        .close = media_openh264_close,
    };
    return esp_video_dec_register(&desc, &ops);
}
