set(ESP_PLAYER_VIDEO_PIPELINE
    "${CMAKE_CURRENT_LIST_DIR}/../managed_components/espressif__esp_player/src/video/player_pipeline_video.c")

set(ESP_PLAYER_MJPEG_RENDER_FPS_MARKER "MJPEG renderer FPS set from AVI extractor")

if(NOT EXISTS "${ESP_PLAYER_VIDEO_PIPELINE}")
    message(FATAL_ERROR
        "ESP-Player source not found at ${ESP_PLAYER_VIDEO_PIPELINE}; refusing to build without the MJPEG AVI FPS correction")
endif()

file(READ "${ESP_PLAYER_VIDEO_PIPELINE}" ESP_PLAYER_VIDEO_PIPELINE_SOURCE)
string(FIND "${ESP_PLAYER_VIDEO_PIPELINE_SOURCE}"
       "${ESP_PLAYER_MJPEG_RENDER_FPS_MARKER}"
       ESP_PLAYER_MJPEG_RENDER_FPS_PATCHED)

if(ESP_PLAYER_MJPEG_RENDER_FPS_PATCHED EQUAL -1)
    # Match the assignment independently of indentation and spacing.
    # The managed component's formatting can differ between ESP-Player releases.
    string(REGEX MATCH
        "[ \t]*video_render_cfg[.]fps[ \t]*=[ \t]*stream->video_side->track_info[.]video_info[.]fps[ \t]*;"
        ESP_PLAYER_RENDER_FPS_ANCHOR
        "${ESP_PLAYER_VIDEO_PIPELINE_SOURCE}")

    if(ESP_PLAYER_RENDER_FPS_ANCHOR STREQUAL "")
        message(FATAL_ERROR
            "ESP-Player renderer FPS assignment was not found in player_pipeline_video.c; refusing to build without the MJPEG AVI FPS correction")
    endif()

    set(ESP_PLAYER_RENDER_FPS_INSERT [=[
            /*
             * ESP-Player 1.0.7 obtains the source FPS from extractor track info.
             * Its decoder INFO event updates width, height, and bitrate, but does
             * not update FPS. The previous workaround ran after decoder startup;
             * the decoder event can queue REPORT_VIDEO_INFO and the state task
             * can create the renderer before that late workaround executes.
             *
             * Restore the extractor's authoritative FPS immediately before the
             * renderer config consumes track_info.video_info.fps.
             */
            if (stream->video_side->track_info.video_info.format == ESP_FOURCC_MJPG &&
                stream->extractor != NULL) {
                int8_t video_idx = -1;
                esp_gmf_element_handle_t extractor_el = player_extractor_el(stream);
                if (extractor_el != NULL &&
                    player_extractor_track_active(extractor_el,
                                                  ESP_EXTRACTOR_STREAM_TYPE_VIDEO,
                                                  &video_idx) == ESP_GMF_ERR_OK &&
                    video_idx >= 0) {
                    esp_extractor_stream_info_t extractor_info = {0};
                    if (player_extractor_get_stream_info(extractor_el,
                                                         ESP_EXTRACTOR_STREAM_TYPE_VIDEO,
                                                         video_idx,
                                                         &extractor_info) == ESP_GMF_ERR_OK &&
                        extractor_info.video_info.fps > 0) {
                        int32_t previous_fps =
                            (int32_t)stream->video_side->track_info.video_info.fps;
                        int32_t source_fps = (int32_t)extractor_info.video_info.fps;
                        stream->video_side->track_info.video_info.fps = source_fps;
                        player_sync_set_video_fps(stream->sync_handle, (float)source_fps);
                        player_sync_enable_video_fps_sync(stream->sync_handle, true);
                        ESP_LOGW(TAG,
                                 "MJPEG renderer FPS set from AVI extractor: previous=%" PRId32
                                 ", source=%" PRId32 ", renderer=%" PRId32,
                                 previous_fps, source_fps, source_fps);
                    } else {
                        ESP_LOGE(TAG, "Could not read MJPEG FPS from AVI extractor");
                    }
                } else {
                    ESP_LOGE(TAG, "Could not find active MJPEG AVI video track");
                }
            }
]=])

    string(REPLACE
        "${ESP_PLAYER_RENDER_FPS_ANCHOR}"
        "${ESP_PLAYER_RENDER_FPS_INSERT}${ESP_PLAYER_RENDER_FPS_ANCHOR}"
        ESP_PLAYER_VIDEO_PIPELINE_SOURCE
        "${ESP_PLAYER_VIDEO_PIPELINE_SOURCE}")

    file(WRITE "${ESP_PLAYER_VIDEO_PIPELINE}" "${ESP_PLAYER_VIDEO_PIPELINE_SOURCE}")
    message(STATUS "ESP-Player: inserted MJPEG AVI FPS correction before renderer FPS assignment")
else()
    message(STATUS "ESP-Player: MJPEG AVI FPS correction already present")
endif()

file(READ "${ESP_PLAYER_VIDEO_PIPELINE}" ESP_PLAYER_VIDEO_PIPELINE_VERIFICATION)
string(FIND "${ESP_PLAYER_VIDEO_PIPELINE_VERIFICATION}"
       "${ESP_PLAYER_MJPEG_RENDER_FPS_MARKER}"
       ESP_PLAYER_MJPEG_RENDER_FPS_VERIFIED)
if(ESP_PLAYER_MJPEG_RENDER_FPS_VERIFIED EQUAL -1)
    message(FATAL_ERROR "ESP-Player MJPEG AVI FPS correction verification failed")
endif()
