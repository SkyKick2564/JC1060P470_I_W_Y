#ifndef MEDIA_MANAGER_VIDEO_BACKEND_H
#define MEDIA_MANAGER_VIDEO_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_codec_dev.h"
#include "esp_video_render.h"

#include "media_manager.h"

#define MEDIA_MANAGER_VIDEO_MAX_WIDTH  1280
#define MEDIA_MANAGER_VIDEO_MAX_HEIGHT 720

typedef void (*media_manager_video_event_callback_t)(
    media_manager_event_t event,
    esp_err_t error,
    void *user_ctx
);

esp_err_t media_manager_video_backend_attach(esp_video_render_handle_t render);
esp_err_t media_manager_video_backend_set_source_info(uint16_t width, uint16_t height);

esp_err_t media_manager_video_play_file(
    const char *path,
    esp_codec_dev_handle_t codec,
    float volume,
    media_manager_video_event_callback_t event_callback,
    void *event_user_ctx,
    bool *stopped
);

void media_manager_notify_codec_opened(bool opened);

#endif /* MEDIA_MANAGER_VIDEO_BACKEND_H */
