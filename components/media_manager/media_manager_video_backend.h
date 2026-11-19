#ifndef MEDIA_MANAGER_VIDEO_BACKEND_H
#define MEDIA_MANAGER_VIDEO_BACKEND_H

#include <stdint.h>

#include "esp_err.h"
#include "esp_video_render.h"

esp_err_t media_manager_video_backend_attach(esp_video_render_handle_t render);
esp_err_t media_manager_video_backend_set_source_info(uint16_t width, uint16_t height);

#endif /* MEDIA_MANAGER_VIDEO_BACKEND_H */
