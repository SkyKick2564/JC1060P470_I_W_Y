set(ESP_PLAYER_VIDEO_PIPELINE
    "${CMAKE_CURRENT_LIST_DIR}/../managed_components/espressif__esp_player/src/video/player_pipeline_video.c")

if(NOT EXISTS "${ESP_PLAYER_VIDEO_PIPELINE}")
    message(WARNING "ESP-Player video pipeline not found; MJPEG AVI FPS correction was not applied")
    return()
endif()

file(READ "${ESP_PLAYER_VIDEO_PIPELINE}" ESP_PLAYER_VIDEO_PIPELINE_SOURCE)

set(ESP_PLAYER_MJPEG_RENDER_FPS_MARKER "Correct MJPEG AVI FPS at renderer creation")
string(FIND "${ESP_PLAYER_VIDEO_PIPELINE_SOURCE}" "${ESP_PLAYER_MJPEG_RENDER_FPS_MARKER}" ESP_PLAYER_MJPEG_RENDER_FPS_PATCHED)

if(ESP_PLAYER_MJPEG_RENDER_FPS_PATCHED EQUAL -1)
    set(ESP_PLAYER_RENDER_FPS_ANCHOR
        "            video_render_cfg.fps = stream->video_side->track_info.video_info.fps;")
    string(FIND "${ESP_PLAYER_VIDEO_PIPELINE_SOURCE}" "${ESP_PLAYER_RENDER_FPS_ANCHOR}" ESP_PLAYER_RENDER_FPS_POSITION)

    if(ESP_PLAYER_RENDER_FPS_POSITION EQUAL -1)
        message(WARNING "ESP-Player renderer FPS assignment was not found; MJPEG AVI FPS correction was not applied")
    else()
        set(ESP_PLAYER_RENDER_FPS_INSERT [=[
            /*
             * The AVI extractor reports the correct 25 FPS, but the MJPEG
             * decoder can replace track_info.fps with its default 10 FPS.
             * Correct the final stream metadata immediately before the
             * renderer and A/V sync clock consume it.
             */
            if (stream->video_side->track_info.video_info.format == ESP_FOURCC_MJPG &&
                stream->video_side->track_info.video_info.width == 1280 &&
                stream->video_side->track_info.video_info.height == 720) {
                int32_t reported_fps = stream->video_side->track_info.video_info.fps;
                stream->video_side->track_info.video_info.fps = 25;
                player_sync_set_video_fps(stream->sync_handle, 25);
                player_sync_enable_video_fps_sync(stream->sync_handle, true);
                ESP_LOGW(TAG,
                         "Correct MJPEG AVI FPS at renderer creation: decoder=%" PRId32 ", renderer=25",
                         reported_fps);
            }
]=])

        string(REPLACE
            "${ESP_PLAYER_RENDER_FPS_ANCHOR}"
            "${ESP_PLAYER_RENDER_FPS_INSERT}${ESP_PLAYER_RENDER_FPS_ANCHOR}"
            ESP_PLAYER_VIDEO_PIPELINE_SOURCE
            "${ESP_PLAYER_VIDEO_PIPELINE_SOURCE}")
        file(WRITE "${ESP_PLAYER_VIDEO_PIPELINE}" "${ESP_PLAYER_VIDEO_PIPELINE_SOURCE}")
        message(STATUS "ESP-Player: installed MJPEG AVI FPS correction at renderer setup")
    endif()
else()
    message(STATUS "ESP-Player: MJPEG AVI FPS correction already installed")
endif()
