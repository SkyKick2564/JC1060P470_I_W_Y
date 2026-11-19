#ifndef MEDIA_MANAGER_VIDEO_BACKEND_H
#define MEDIA_MANAGER_VIDEO_BACKEND_H

#include "esp_err.h"
#include "esp_video_render.h"
#include "lvgl.h"

esp_err_t media_manager_video_backend_attach(esp_video_render_handle_t render);

#endif /* MEDIA_MANAGER_VIDEO_BACKEND_H */
