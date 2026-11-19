#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_video_render_backend.h"

#include "media_manager.h"
#include "media_manager_video_backend.h"

#define TAG "MEDIA_VIDEO_BACKEND"

typedef struct {
    lv_obj_t *widget;
    uint16_t width;
    uint16_t height;
} video_target_config_t;

typedef struct {
    lv_obj_t *widget;
    const void *previous_src;
    uint8_t *buffer;
    esp_video_render_fb_t fb;
    lv_image_dsc_t image;
    bool source_bound;
} video_backend_context_t;

static portMUX_TYPE s_target_mux = portMUX_INITIALIZER_UNLOCKED;
static video_target_config_t s_target;

static esp_video_render_err_t video_backend_init(
    void *cfg,
    int cfg_size,
    esp_video_render_backend_handle_t *out_backend)
{
    if (cfg == NULL || out_backend == NULL ||
        cfg_size < (int)sizeof(video_target_config_t)) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    const video_target_config_t *target = (const video_target_config_t *)cfg;
    if (target->widget == NULL || target->width == 0 || target->height == 0) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    video_backend_context_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }

    if (!lvgl_port_lock(0)) {
        free(ctx);
        return ESP_VIDEO_RENDER_ERR_TIMEOUT;
    }

    if (!lv_obj_is_valid(target->widget) ||
        !lv_obj_check_type(target->widget, &lv_image_class)) {
        lvgl_port_unlock();
        free(ctx);
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    ctx->widget = target->widget;
    ctx->previous_src = lv_image_get_src(ctx->widget);

    lvgl_port_unlock();

    ctx->fb.info.format = ESP_VIDEO_RENDER_FORMAT_RGB565;
    ctx->fb.info.width = target->width;
    ctx->fb.info.height = target->height;
    ctx->fb.size = (uint32_t)target->width * target->height * 2U;

    ctx->image.header.magic = LV_IMAGE_HEADER_MAGIC;
    ctx->image.header.cf = LV_COLOR_FORMAT_RGB565;
    ctx->image.header.w = target->width;
    ctx->image.header.h = target->height;
    ctx->image.header.stride = (uint16_t)(target->width * 2U);
    ctx->image.data_size = ctx->fb.size;
    ctx->image.data = NULL;

    *out_backend = (esp_video_render_backend_handle_t)ctx;
    ESP_LOGI(TAG, "EEZ Image target attached: %ux%u",
             (unsigned)target->width, (unsigned)target->height);
    return ESP_VIDEO_RENDER_ERR_OK;
}

static bool video_backend_with_gram(esp_video_render_backend_handle_t backend)
{
    (void)backend;
    return true;
}

static esp_video_render_err_t video_backend_get_display_info(
    esp_video_render_backend_handle_t backend,
    esp_video_render_disp_info_t *info)
{
    if (backend == NULL || info == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    const video_backend_context_t *ctx = backend;
    info->format = ctx->fb.info.format;
    info->width = ctx->fb.info.width;
    info->height = ctx->fb.info.height;
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t video_backend_get_fb(
    esp_video_render_backend_handle_t backend,
    esp_video_render_fb_t *fb)
{
    if (backend == NULL || fb == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    video_backend_context_t *ctx = backend;
    if (ctx->buffer == NULL) {
        ctx->buffer = heap_caps_malloc(
            ctx->fb.size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ctx->buffer == NULL) {
            ctx->buffer = heap_caps_malloc(ctx->fb.size, MALLOC_CAP_8BIT);
        }
        if (ctx->buffer == NULL) {
            ESP_LOGE(TAG, "Cannot allocate %u-byte video frame buffer",
                     (unsigned)ctx->fb.size);
            return ESP_VIDEO_RENDER_ERR_NO_MEM;
        }

        ctx->fb.data = ctx->buffer;
        ctx->image.data = ctx->buffer;
    }

    *fb = ctx->fb;
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t video_backend_lock_fb(
    esp_video_render_backend_handle_t backend,
    esp_video_render_fb_t *fb,
    bool lock)
{
    (void)backend;
    (void)fb;

    if (lock) {
        return lvgl_port_lock(0) ?
            ESP_VIDEO_RENDER_ERR_OK : ESP_VIDEO_RENDER_ERR_TIMEOUT;
    }

    lvgl_port_unlock();
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t video_backend_write_fb(
    esp_video_render_backend_handle_t backend,
    esp_video_render_fb_t *fb,
    const esp_video_render_rect_t *dirty_rect,
    const esp_video_render_pos_t *pos)
{
    (void)dirty_rect;
    (void)pos;

    if (backend == NULL || fb == NULL || fb->data == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    video_backend_context_t *ctx = backend;
    if (fb->size != ctx->fb.size ||
        fb->info.width != ctx->fb.info.width ||
        fb->info.height != ctx->fb.info.height) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    if (!lvgl_port_lock(0)) {
        return ESP_VIDEO_RENDER_ERR_TIMEOUT;
    }

    if (!lv_obj_is_valid(ctx->widget) ||
        !lv_obj_check_type(ctx->widget, &lv_image_class)) {
        lvgl_port_unlock();
        return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
    }

    if (fb->data != ctx->buffer) {
        memcpy(ctx->buffer, fb->data, ctx->fb.size);
    }

    if (!ctx->source_bound) {
        lv_image_set_src(ctx->widget, &ctx->image);
        ctx->source_bound = true;
    }

    lv_obj_invalidate(ctx->widget);
    lvgl_port_unlock();
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t video_backend_deinit(
    esp_video_render_backend_handle_t backend)
{
    if (backend == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    video_backend_context_t *ctx = backend;

    if (lvgl_port_lock(0)) {
        if (lv_obj_is_valid(ctx->widget) &&
            lv_obj_check_type(ctx->widget, &lv_image_class) &&
            lv_image_get_src(ctx->widget) == &ctx->image) {
            lv_image_set_src(ctx->widget, ctx->previous_src);
        }
        lvgl_port_unlock();
    }

    if (ctx->buffer != NULL) {
        heap_caps_free(ctx->buffer);
    }
    free(ctx);
    return ESP_VIDEO_RENDER_ERR_OK;
}

static const esp_video_render_backend_ops_t s_video_backend_ops = {
    .init = video_backend_init,
    .with_gram = video_backend_with_gram,
    .get_display_info = video_backend_get_display_info,
    .get_fb = video_backend_get_fb,
    .lock_fb = video_backend_lock_fb,
    .write_fb = video_backend_write_fb,
    .deinit = video_backend_deinit,
};

esp_err_t media_manager_set_video_widget(lv_obj_t *image_widget)
{
    video_target_config_t target = {0};

    if (image_widget != NULL) {
        if (!lvgl_port_lock(0)) {
            return ESP_ERR_TIMEOUT;
        }

        bool valid = lv_obj_is_valid(image_widget) &&
                     lv_obj_check_type(image_widget, &lv_image_class);
        int32_t width = valid ? lv_obj_get_width(image_widget) : 0;
        int32_t height = valid ? lv_obj_get_height(image_widget) : 0;

        lvgl_port_unlock();

        if (!valid || width <= 0 || height <= 0 ||
            width > UINT16_MAX || height > UINT16_MAX) {
            ESP_LOGE(TAG, "Video target must be a sized LVGL Image widget");
            return ESP_ERR_INVALID_ARG;
        }

        target.widget = image_widget;
        target.width = (uint16_t)width;
        target.height = (uint16_t)height;
    }

    portENTER_CRITICAL(&s_target_mux);
    s_target = target;
    portEXIT_CRITICAL(&s_target_mux);

    ESP_LOGI(TAG, image_widget == NULL ?
             "EEZ Image target cleared" : "EEZ Image target registered");
    return ESP_OK;
}

esp_err_t media_manager_video_backend_attach(esp_video_render_handle_t render)
{
    if (render == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    video_target_config_t target;
    portENTER_CRITICAL(&s_target_mux);
    target = s_target;
    portEXIT_CRITICAL(&s_target_mux);

    if (target.widget == NULL || target.width == 0 || target.height == 0) {
        ESP_LOGE(TAG, "No EEZ Image target registered");
        return ESP_ERR_INVALID_STATE;
    }

    esp_video_render_backend_cfg_t backend_cfg = {
        .ops = &s_video_backend_ops,
        .cfg = &target,
        .cfg_size = sizeof(target),
    };

    esp_video_render_err_t err = esp_video_render_set_display(
        render, &backend_cfg);
    if (err != ESP_VIDEO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "Attach EEZ Image backend failed: %d", (int)err);
        return ESP_FAIL;
    }

    return ESP_OK;
}
