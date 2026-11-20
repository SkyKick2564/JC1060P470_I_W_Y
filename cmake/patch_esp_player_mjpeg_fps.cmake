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
             * The MJPEG decoder may replace the AVI extractor's frame rate
             * with its default value (10 FPS). Restore the source frame rate
             * from the extractor immediately before configuring the renderer
             * and the A/V synchronization clock.
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
                        int32_t decoder_fps =
                            (int32_t)stream->video_side->track_info.video_info.fps;
                        int32_t source_fps = (int32_t)extractor_info.video_info.fps;
                        stream->video_side->track_info.video_info.fps = source_fps;
                        player_sync_set_video_fps(stream->sync_handle, (float)source_fps);
                        player_sync_enable_video_fps_sync(stream->sync_handle, true);
                        ESP_LOGW(TAG,
                                 "Correct MJPEG FPS at renderer creation: decoder=%" PRId32
                                 ", extractor=%" PRId32 ", renderer=%" PRId32,
                                 decoder_fps, source_fps, source_fps);
                    }
                }
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
