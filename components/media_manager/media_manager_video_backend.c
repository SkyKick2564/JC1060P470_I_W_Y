
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_video_render_backend.h"

#include "media_manager.h"
#include "media_manager_video_backend.h"

#define TAG "MEDIA_VIDEO_BACKEND"

#define MEDIA_VIDEO_SOURCE_INFO_READY BIT(0)

/* PPA output buffers in PSRAM must satisfy both L1 and L2 cache alignment. */
#define MEDIA_VIDEO_PPA_BUFFER_ALIGNMENT CONFIG_CACHE_L2_CACHE_LINE_SIZE

typedef struct {
    lv_obj_t *widget;
} video_target_config_t;

typedef struct {
    lv_obj_t *widget;
    const void *previous_src;
    uint8_t *buffers[2];
    int display_buffer_index;
    esp_video_render_fb_t fb;
    lv_image_dsc_t image;
    EventGroupHandle_t events;
    SemaphoreHandle_t lock;
    uint16_t source_width;
    uint16_t source_height;
    uint16_t output_width;
    uint16_t output_height;
    bool source_info_valid;
    bool source_bound;
    bool crop_logged;
    bool external_fb_logged;
    uint32_t profile_frames;
    int64_t profile_start_us;
    int64_t profile_lock_wait_max_us;
    int64_t profile_total_us;
} video_backend_context_t;

static portMUX_TYPE s_target_mux = portMUX_INITIALIZER_UNLOCKED;
static lv_obj_t *s_target_widget;
static video_backend_context_t *s_active_backend;

/*
 * ESP32-P4 PPA SRM has a Q4 scale coefficient (1/16 steps).
 * Select the next scale step that covers the widget, then crop the
 * centered visible area from the larger PPA output.
 */
static bool video_backend_get_ppa_cover_crop(
    uint16_t source_width,
    uint16_t source_height,
    uint16_t target_width,
    uint16_t target_height,
    uint16_t *scaled_width,
    uint16_t *scaled_height,
    uint16_t *crop_x,
    uint16_t *crop_y)
{
    if (source_width == 0 || source_height == 0 ||
        target_width == 0 || target_height == 0 ||
        scaled_width == NULL || scaled_height == NULL ||
        crop_x == NULL || crop_y == NULL) {
        return false;
    }

    uint32_t scale_x_steps =
        ((uint32_t)target_width * 16U + source_width - 1U) / source_width;
    uint32_t scale_y_steps =
        ((uint32_t)target_height * 16U + source_height - 1U) / source_height;
    uint32_t scale_steps = scale_x_steps > scale_y_steps ?
                           scale_x_steps : scale_y_steps;

    uint32_t actual_width = ((uint32_t)source_width * scale_steps) / 16U;
    uint32_t actual_height = ((uint32_t)source_height * scale_steps) / 16U;
    if (actual_width < target_width || actual_height < target_height ||
        actual_width > MEDIA_MANAGER_VIDEO_MAX_WIDTH ||
        actual_height > MEDIA_MANAGER_VIDEO_MAX_HEIGHT) {
        return false;
    }

    *scaled_width = (uint16_t)actual_width;
    *scaled_height = (uint16_t)actual_height;
    *crop_x = (uint16_t)((actual_width - target_width) / 2U);
    *crop_y = (uint16_t)((actual_height - target_height) / 2U);
    return true;
}

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
    if (target->widget == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    video_backend_context_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }

    ctx->events = xEventGroupCreate();
    ctx->lock = xSemaphoreCreateMutex();
    if (ctx->events == NULL || ctx->lock == NULL) {
        if (ctx->events != NULL) {
            vEventGroupDelete(ctx->events);
        }
        if (ctx->lock != NULL) {
            vSemaphoreDelete(ctx->lock);
        }
        free(ctx);
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }

    if (!lvgl_port_lock(0)) {
        vEventGroupDelete(ctx->events);
        vSemaphoreDelete(ctx->lock);
        free(ctx);
        return ESP_VIDEO_RENDER_ERR_TIMEOUT;
    }

    if (!lv_obj_is_valid(target->widget) ||
        !lv_obj_check_type(target->widget, &lv_image_class)) {
        lvgl_port_unlock();
        vEventGroupDelete(ctx->events);
        vSemaphoreDelete(ctx->lock);
        free(ctx);
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    ctx->widget = target->widget;
    ctx->previous_src = lv_image_get_src(ctx->widget);
    int32_t output_width = lv_obj_get_width(ctx->widget);
    int32_t output_height = lv_obj_get_height(ctx->widget);
    if (output_width <= 0 || output_height <= 0 ||
        output_width > MEDIA_MANAGER_VIDEO_MAX_WIDTH ||
        output_height > MEDIA_MANAGER_VIDEO_MAX_HEIGHT) {
        lvgl_port_unlock();
        vEventGroupDelete(ctx->events);
        vSemaphoreDelete(ctx->lock);
        free(ctx);
        ESP_LOGE(TAG, "Invalid video widget size: %ldx%ld",
                 (long)output_width, (long)output_height);
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    ctx->output_width = (uint16_t)output_width;
    ctx->output_height = (uint16_t)output_height;
    lvgl_port_unlock();

    ctx->display_buffer_index = -1;
    ctx->fb.info.format = ESP_VIDEO_RENDER_FORMAT_RGB565;
    /*
     * The current player targets a 389x200 EEZ Image widget. PPA's Q4
     * scale quantization produces a 400x225 frame for 1280x720 input.
     * Keep the renderer canvas at that actual frame size instead of a
     * 1280x720 canvas; the old canvas forced an unnecessary full-frame
     * composition pass and two 1.8 MiB PSRAM buffers.
     */
    ctx->fb.info.width = 400;
    ctx->fb.info.height = 225;
    ctx->fb.size = (uint32_t)ctx->fb.info.width *
                   ctx->fb.info.height * 2U;

    portENTER_CRITICAL(&s_target_mux);
    s_active_backend = ctx;
    portEXIT_CRITICAL(&s_target_mux);

    *out_backend = (esp_video_render_backend_handle_t)ctx;
    ESP_LOGI(TAG, "EEZ Image target attached; render canvas %ux%u",
             (unsigned)ctx->fb.info.width, (unsigned)ctx->fb.info.height);
    return ESP_VIDEO_RENDER_ERR_OK;
}

static bool video_backend_with_gram(esp_video_render_backend_handle_t backend)
{
    (void)backend;

    /*
     * This backend publishes a memory image to LVGL; it does not write into
     * display GRAM. Returning false makes the renderer acquire our persistent
     * destination framebuffer through get_fb(), rather than handing write_fb()
     * its transient render-pool framebuffer.
     */
    return false;
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

static bool video_backend_ensure_buffers(video_backend_context_t *ctx)
{
    for (int i = 0; i < 2; i++) {
        if (ctx->buffers[i] == NULL) {
            ctx->buffers[i] = heap_caps_aligned_calloc(
                MEDIA_VIDEO_PPA_BUFFER_ALIGNMENT, 1, ctx->fb.size,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (ctx->buffers[i] == NULL) {
                ESP_LOGE(TAG, "Cannot allocate video frame buffer %d (%u bytes)",
                         i, (unsigned)ctx->fb.size);
                for (int j = 0; j < 2; j++) {
                    if (ctx->buffers[j] != NULL) {
                        heap_caps_free(ctx->buffers[j]);
                        ctx->buffers[j] = NULL;
                    }
                }
                return false;
            }
        }
    }

    return true;
}

static esp_video_render_err_t video_backend_get_fb(
    esp_video_render_backend_handle_t backend,
    esp_video_render_fb_t *fb)
{
    if (backend == NULL || fb == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    video_backend_context_t *ctx = backend;
    if (!video_backend_ensure_buffers(ctx)) {
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }

    *fb = ctx->fb;
    fb->data = ctx->buffers[ctx->display_buffer_index == 0 ? 1 : 0];
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t video_backend_lock_fb(
    esp_video_render_backend_handle_t backend,
    esp_video_render_fb_t *fb,
    bool lock)
{
    (void)fb;
    if (backend == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    video_backend_context_t *ctx = backend;
    if (lock) {
        if (xSemaphoreTake(ctx->lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
            ESP_LOGW(TAG, "Framebuffer lock timed out");
            return ESP_VIDEO_RENDER_ERR_TIMEOUT;
        }
        return ESP_VIDEO_RENDER_ERR_OK;
    }

    xSemaphoreGive(ctx->lock);
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
    int64_t frame_start_us = esp_timer_get_time();
    if (fb->size != ctx->fb.size ||
        fb->info.format != ESP_VIDEO_RENDER_FORMAT_RGB565 ||
        fb->info.width != ctx->fb.info.width ||
        fb->info.height != ctx->fb.info.height) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    EventBits_t bits = xEventGroupWaitBits(
        ctx->events,
        MEDIA_VIDEO_SOURCE_INFO_READY,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(5000));
    if ((bits & MEDIA_VIDEO_SOURCE_INFO_READY) == 0) {
        ESP_LOGE(TAG, "Timed out waiting for parsed video dimensions");
        return ESP_VIDEO_RENDER_ERR_TIMEOUT;
    }

    /*
     * Do not use ctx->lock here. The renderer may call write_fb() while
     * holding that framebuffer lock. The playback task must be able to
     * publish source dimensions concurrently to release this wait.
     */
    bool source_info_valid;
    uint16_t source_width;
    uint16_t source_height;
    portENTER_CRITICAL(&s_target_mux);
    source_info_valid = ctx->source_info_valid;
    source_width = ctx->source_width;
    source_height = ctx->source_height;
    portEXIT_CRITICAL(&s_target_mux);

    if (!source_info_valid) {
        return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
    }

    uint16_t scaled_width = 0;
    uint16_t scaled_height = 0;
    uint16_t crop_x = 0;
    uint16_t crop_y = 0;
    if (!video_backend_get_ppa_cover_crop(
            source_width,
            source_height,
            ctx->output_width,
            ctx->output_height,
            &scaled_width,
            &scaled_height,
            &crop_x,
            &crop_y)) {
        ESP_LOGE(TAG,
                 "Cannot crop PPA output %ux%u to widget %ux%u",
                 (unsigned)source_width, (unsigned)source_height,
                 (unsigned)ctx->output_width, (unsigned)ctx->output_height);
        return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
    }

    int display_index = -1;
    for (int i = 0; i < 2; i++) {
        if (fb->data == ctx->buffers[i]) {
            display_index = i;
            break;
        }
    }

    /*
     * esp_video_render may pass its own render-pool framebuffer to the
     * backend instead of the buffer previously returned by get_fb().
     * The incoming frame is valid for this callback, but its lifetime is
     * owned by the renderer. Copy it into our persistent double buffer before
     * publishing it to LVGL, which may read the image asynchronously.
     */
    if (display_index < 0) {
        /*
         * Some renderer paths provide their own framebuffer without calling
         * this backend's get_fb(). Ensure the LVGL-owned destination buffers
         * exist before copying; otherwise memcpy() would write through NULL.
         */
        if (!video_backend_ensure_buffers(ctx)) {
            return ESP_VIDEO_RENDER_ERR_NO_MEM;
        }
        display_index = ctx->display_buffer_index == 0 ? 1 : 0;
        if (ctx->buffers[display_index] == NULL ||
            fb->data == ctx->buffers[display_index]) {
            ESP_LOGE(TAG, "Invalid framebuffer copy destination/source");
            return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
        }
        memcpy(ctx->buffers[display_index], fb->data, fb->size);
        if (!ctx->external_fb_logged) {
            ESP_LOGW(TAG,
                     "Renderer supplied a render-pool framebuffer; copying into LVGL-owned buffer");
            ctx->external_fb_logged = true;
        }
    }

    /*
     * Video is real-time data: never stall the decoder waiting for LVGL.
     * LVGL can hold its port lock while rendering/scaling the previous frame.
     * If it is still busy after 1 ms, drop this frame and let the renderer
     * reuse the framebuffer on the next cycle. Blocking here propagates
     * backpressure into decoding and audio/video playback.
     */
    int64_t lock_start_us = esp_timer_get_time();
    if (!lvgl_port_lock(1)) {
        ESP_LOGD(TAG, "Drop late video frame: LVGL is busy");
        return ESP_VIDEO_RENDER_ERR_OK;
    }

    int64_t lock_wait_us = esp_timer_get_time() - lock_start_us;
    if (lock_wait_us > ctx->profile_lock_wait_max_us) {
        ctx->profile_lock_wait_max_us = lock_wait_us;
    }

    if (!lv_obj_is_valid(ctx->widget) ||
        !lv_obj_check_type(ctx->widget, &lv_image_class)) {
        lvgl_port_unlock();
        return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
    }

    /*
     * The PPA output is larger than the widget because P4 SRM scale is
     * quantized to 1/16 steps. Crop its centered visible region while keeping
     * the render-canvas stride, so LVGL receives exactly the widget size
     * without applying another software scale.
     */
    size_t image_offset =
        (size_t)crop_y * ctx->fb.info.width * 2U +
        (size_t)crop_x * 2U;
    if (image_offset >= ctx->fb.size) {
        lvgl_port_unlock();
        return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
    }

    ctx->image.header.magic = LV_IMAGE_HEADER_MAGIC;
    ctx->image.header.cf = LV_COLOR_FORMAT_RGB565;
    ctx->image.header.w = ctx->output_width;
    ctx->image.header.h = ctx->output_height;
    ctx->image.header.stride = ctx->fb.info.width * 2U;
    ctx->image.data_size = ctx->fb.size - image_offset;
    ctx->image.data = ctx->buffers[display_index] + image_offset;
    ctx->display_buffer_index = display_index;

    if (!ctx->crop_logged) {
        ESP_LOGI(TAG,
                 "PPA Q4 crop: render=%ux%u widget=%ux%u offset=(%u,%u)",
                 (unsigned)scaled_width, (unsigned)scaled_height,
                 (unsigned)ctx->output_width, (unsigned)ctx->output_height,
                 (unsigned)crop_x, (unsigned)crop_y);
        ctx->crop_logged = true;
    }

    if (!ctx->source_bound) {
        lv_image_set_src(ctx->widget, &ctx->image);
        ctx->source_bound = true;
    }

    lv_obj_invalidate(ctx->widget);
    lvgl_port_unlock();

    int64_t elapsed_us = esp_timer_get_time() - frame_start_us;
    ctx->profile_total_us += elapsed_us;
    ctx->profile_frames++;
    if (ctx->profile_start_us == 0) {
        ctx->profile_start_us = frame_start_us;
    }
    if (ctx->profile_frames >= 50) {
        int64_t window_us = esp_timer_get_time() - ctx->profile_start_us;
        ESP_LOGI(TAG,
                 "Frame profile: count=%" PRIu32 " rate=%.2f fps avg_write=%.2f ms max_lvgl_wait=%.2f ms",
                 ctx->profile_frames,
                 window_us > 0 ? (double)ctx->profile_frames * 1000000.0 / (double)window_us : 0.0,
                 (double)ctx->profile_total_us / (double)ctx->profile_frames / 1000.0,
                 (double)ctx->profile_lock_wait_max_us / 1000.0);
        ctx->profile_frames = 0;
        ctx->profile_start_us = 0;
        ctx->profile_total_us = 0;
        ctx->profile_lock_wait_max_us = 0;
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t video_backend_deinit(
    esp_video_render_backend_handle_t backend)
{
    if (backend == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    video_backend_context_t *ctx = backend;

    portENTER_CRITICAL(&s_target_mux);
    if (s_active_backend == ctx) {
        s_active_backend = NULL;
    }
    portEXIT_CRITICAL(&s_target_mux);

    /*
     * The LVGL image may still reference ctx->image and either PSRAM buffer.
     * Never free those buffers unless the LVGL lock was acquired and the
     * widget source has been detached. A zero-timeout lock here could fail
     * during a display refresh and leave LVGL with a dangling image pointer.
     */
    if (!lvgl_port_lock(1000)) {
        ESP_LOGE(TAG, "Timed out detaching LVGL image during backend deinit");
        return ESP_VIDEO_RENDER_ERR_TIMEOUT;
    }

    if (lv_obj_is_valid(ctx->widget) &&
        lv_obj_check_type(ctx->widget, &lv_image_class) &&
        lv_image_get_src(ctx->widget) == &ctx->image) {
        lv_image_set_src(ctx->widget, ctx->previous_src);
    }
    lvgl_port_unlock();

    for (int i = 0; i < 2; i++) {
        if (ctx->buffers[i] != NULL) {
            heap_caps_free(ctx->buffers[i]);
        }
    }
    vEventGroupDelete(ctx->events);
    vSemaphoreDelete(ctx->lock);
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
    if (image_widget != NULL) {
        if (!lvgl_port_lock(0)) {
            return ESP_ERR_TIMEOUT;
        }

        bool valid = lv_obj_is_valid(image_widget) &&
                     lv_obj_check_type(image_widget, &lv_image_class);

        lvgl_port_unlock();

        if (!valid) {
            ESP_LOGE(TAG, "Video target must be an LVGL Image widget");
            return ESP_ERR_INVALID_ARG;
        }
    }

    portENTER_CRITICAL(&s_target_mux);
    s_target_widget = image_widget;
    portEXIT_CRITICAL(&s_target_mux);

    if (image_widget == NULL) {
        ESP_LOGI(TAG, "EEZ Image target cleared");
    } else {
        ESP_LOGI(TAG, "EEZ Image target registered");
    }
    return ESP_OK;
}

esp_err_t media_manager_video_backend_get_output_size(
    uint16_t *width,
    uint16_t *height)
{
    if (width == NULL || height == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    video_backend_context_t *ctx;
    portENTER_CRITICAL(&s_target_mux);
    ctx = s_active_backend;
    portEXIT_CRITICAL(&s_target_mux);

    if (ctx == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    *width = ctx->output_width;
    *height = ctx->output_height;
    return ESP_OK;
}

bool media_manager_video_backend_has_target(void)
{
    bool has_target;

    portENTER_CRITICAL(&s_target_mux);
    has_target = s_target_widget != NULL;
    portEXIT_CRITICAL(&s_target_mux);

    return has_target;
}

esp_err_t media_manager_video_backend_set_source_info(
    uint16_t width,
    uint16_t height)
{
    video_backend_context_t *ctx;

    portENTER_CRITICAL(&s_target_mux);
    ctx = s_active_backend;
    portEXIT_CRITICAL(&s_target_mux);

    if (ctx == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    bool valid = width > 0 && height > 0 &&
                 width <= MEDIA_MANAGER_VIDEO_MAX_WIDTH &&
                 height <= MEDIA_MANAGER_VIDEO_MAX_HEIGHT;

    /*
     * Source metadata has a different lifetime from framebuffer access.
     * Protect it with the short global critical section instead of ctx->lock:
     * write_fb() can be waiting for this event while the renderer owns ctx->lock.
     */
    portENTER_CRITICAL(&s_target_mux);
    ctx->source_width = valid ? width : 0;
    ctx->source_height = valid ? height : 0;
    ctx->source_info_valid = valid;
    portEXIT_CRITICAL(&s_target_mux);

    xEventGroupSetBits(ctx->events, MEDIA_VIDEO_SOURCE_INFO_READY);

    if (!valid) {
        ESP_LOGE(TAG, "Video resolution %ux%u exceeds supported maximum %ux%u",
                 (unsigned)width, (unsigned)height,
                 MEDIA_MANAGER_VIDEO_MAX_WIDTH, MEDIA_MANAGER_VIDEO_MAX_HEIGHT);
        return ESP_ERR_NOT_SUPPORTED;
    }

    ESP_LOGI(TAG, "Video source resolution: %ux%u",
             (unsigned)width, (unsigned)height);
    return ESP_OK;
}

esp_err_t media_manager_video_backend_attach(esp_video_render_handle_t render)
{
    if (render == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lv_obj_t *widget;
    portENTER_CRITICAL(&s_target_mux);
    widget = s_target_widget;
    portEXIT_CRITICAL(&s_target_mux);

    if (widget == NULL) {
        ESP_LOGE(TAG, "No EEZ Image target registered");
        return ESP_ERR_INVALID_STATE;
    }

    video_target_config_t target = {
        .widget = widget,
    };

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
