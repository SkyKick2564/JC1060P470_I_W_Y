#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
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
    uint32_t pending_size;\n    uint32_t pending_consumed;
    uint32_t width;
    uint32_t height;
    int frame_type;
    int64_t pending_pts;
    int64_t pending_dts;
    bool have_sps;
    bool have_pps;
} media_openh264_t;

static const esp_video_codec_pixel_fmt_t s_output_formats[] = {
    ESP_VIDEO_CODEC_PIXEL_FMT_YUV420P,
};

static void *media_openh264_alloc(size_t size)
{
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == NULL) {
        ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    }
    return ptr;
}

static esp_vc_err_t media_openh264_get_caps(esp_video_dec_caps_t *caps)
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
    param.eEcActiveIdc = ERROR_CON_SLICE_MV_COPY_CROSS_IDR_FREEZE_RES_CHANGE;
    param.sVideoProperty.size = sizeof(param.sVideoProperty);
    param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;

    if (ctx->decoder->Initialize(&param) != 0) {
        WelsDestroyDecoder(ctx->decoder);
        free(ctx);
        return ESP_VC_ERR_FAIL;
    }

    *handle = ctx;
    ESP_LOGI(TAG, "OpenH264 Main/High-capable software decoder opened");
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
    output->frame_type = (esp_video_codec_frame_type_t)ctx->frame_type;
    input->consumed = 0;
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
        output->decoded_size = 0;
        return ESP_VC_ERR_NO_MORE_DATA;
    }

    /* MP4/AVCC samples store a 4-byte big-endian NAL length before each NAL.
     * OpenH264's decoder API expects Annex-B start codes and one NAL at a time. */
    heap_caps_free(ctx->annexb);
    ctx->annexb = (uint8_t *)media_openh264_alloc(input->size + 4);
    if (ctx->annexb == NULL) {
        return ESP_VC_ERR_NO_MEMORY;
    }

    uint32_t src_offset = 0;
    bool produced = false;
    uint8_t *planes[3] = { NULL, NULL, NULL };
    SBufferInfo info = {};

    while (src_offset < input->size) {
        uint32_t nal_size = 0;
        uint32_t prefix_size = 0;
        const uint8_t *nal = NULL;

        if (input->size - src_offset >= 4 &&
            input->data[src_offset] == 0 && input->data[src_offset + 1] == 0 &&
            (input->data[src_offset + 2] == 1 ||
             (input->data[src_offset + 2] == 0 && input->data[src_offset + 3] == 1))) {
            prefix_size = (input->data[src_offset + 2] == 1) ? 3 : 4;
            uint32_t start = src_offset + prefix_size;
            uint32_t end = start;
            while (end + 3 < input->size &&
                   !(input->data[end] == 0 && input->data[end + 1] == 0 &&
                     (input->data[end + 2] == 1 ||
                      (input->data[end + 2] == 0 && input->data[end + 3] == 1)))) {
                end++;
            }
            if (end + 3 >= input->size) {
                end = input->size;
            }
            nal = input->data + start;
            nal_size = end - start;
            src_offset = end;
        } else {
            if (input->size - src_offset < 4) {
                heap_caps_free(ctx->annexb);
                ctx->annexb = NULL;
                return ESP_VC_ERR_WRONG_DATA;
            }
            nal_size = ((uint32_t)input->data[src_offset] << 24) |
                       ((uint32_t)input->data[src_offset + 1] << 16) |
                       ((uint32_t)input->data[src_offset + 2] << 8) |
                       input->data[src_offset + 3];
            if (nal_size == 0 || nal_size > input->size - src_offset - 4) {
                heap_caps_free(ctx->annexb);
                ctx->annexb = NULL;
                return ESP_VC_ERR_WRONG_DATA;
            }
            nal = input->data + src_offset + 4;
            src_offset += 4 + nal_size;
        }

        if (nal == NULL || nal_size == 0 || nal_size > input->size) {
            heap_caps_free(ctx->annexb);
            ctx->annexb = NULL;
            return ESP_VC_ERR_WRONG_DATA;
        }

        uint8_t nal_type = nal[0] & 0x1F;
        ctx->have_sps |= (nal_type == 7);
        ctx->have_pps |= (nal_type == 8);

        ctx->annexb[0] = 0;
        ctx->annexb[1] = 0;
        ctx->annexb[2] = 0;
        ctx->annexb[3] = 1;
        memcpy(ctx->annexb + 4, nal, nal_size);

        memset(&info, 0, sizeof(info));
        planes[0] = planes[1] = planes[2] = NULL;
        ctx->decoder->DecodeFrameNoDelay(
            ctx->annexb, (int)(nal_size + 4), planes, &info);

        if (info.iBufferStatus == 1 && planes[0] != NULL &&
            planes[1] != NULL && planes[2] != NULL) {
            const SSysMEMBuffer *sys = &info.UsrData.sSystemBuffer;
            uint32_t width = (uint32_t)sys->iWidth;
            uint32_t height = (uint32_t)sys->iHeight;
            uint32_t chroma_width = (width + 1) / 2;
            uint32_t chroma_height = (height + 1) / 2;
            uint32_t required = width * height +
                               2 * chroma_width * chroma_height;
            uint8_t *frame = (uint8_t *)media_openh264_alloc(required);
            if (frame == NULL) {
                heap_caps_free(ctx->annexb);
                ctx->annexb = NULL;
                return ESP_VC_ERR_NO_MEMORY;
            }

            uint8_t *dst_y = frame;
            uint8_t *dst_u = dst_y + width * height;
            uint8_t *dst_v = dst_u + chroma_width * chroma_height;
            for (uint32_t row = 0; row < height; row++) {
                memcpy(dst_y + row * width,
                       planes[0] + row * sys->iStride[0], width);
            }
            for (uint32_t row = 0; row < chroma_height; row++) {
                memcpy(dst_u + row * chroma_width,
                       planes[1] + row * sys->iStride[1], chroma_width);
                memcpy(dst_v + row * chroma_width,
                       planes[2] + row * sys->iStride[1], chroma_width);
            }

            heap_caps_free(ctx->pending_frame);
            ctx->pending_frame = frame;
            ctx->pending_size = required;\n            ctx->pending_consumed = input->size;
            ctx->width = width;
            ctx->height = height;
            ctx->pending_pts = input->pts;
            ctx->pending_dts = input->dts;
            ctx->frame_type = (nal_type == 5) ?
                ESP_VIDEO_CODEC_FRAME_TYPE_IDR : ESP_VIDEO_CODEC_FRAME_TYPE_NONE;
            produced = true;
        }
    }

    input->consumed = input->size;
    heap_caps_free(ctx->annexb);
    ctx->annexb = NULL;

    if (!produced) {
        output->decoded_size = 0;
        output->pts = input->pts;
        output->dts = input->dts;
        return ESP_VC_ERR_OK;
    }
    return media_openh264_copy_pending(ctx, input, output);
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
