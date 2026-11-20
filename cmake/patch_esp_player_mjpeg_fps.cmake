set(ESP_PLAYER_VIDEO_PIPELINE
    "${CMAKE_CURRENT_LIST_DIR}/../managed_components/espressif__esp_player/src/video/player_pipeline_video.c")

if(NOT EXISTS "${ESP_PLAYER_VIDEO_PIPELINE}")
    message(FATAL_ERROR "ESP-Player source not found: ${ESP_PLAYER_VIDEO_PIPELINE}")
endif()

file(READ "${ESP_PLAYER_VIDEO_PIPELINE}" ESP_PLAYER_VIDEO_PIPELINE_SOURCE)

# Apply the AVI FPS correction only after the decoder has reported its stream
# information. The decoder can overwrite track_info after the extractor runs.
set(ESP_PLAYER_MJPEG_FPS_MARKER "Correct MJPEG AVI FPS after decoder stream info")

string(FIND "${ESP_PLAYER_VIDEO_PIPELINE_SOURCE}" "${ESP_PLAYER_MJPEG_FPS_MARKER}" ESP_PLAYER_MJPEG_FPS_PATCHED)
if(ESP_PLAYER_MJPEG_FPS_PATCHED EQUAL -1)
    set(ESP_PLAYER_MJPEG_FPS_INSERT_BEFORE [=[
    if (run_ret != ESP_PLAYER_ERR_OK) {
        return run_ret;
    }
    return ESP_PLAYER_ERR_OK;
}=])

    set(ESP_PLAYER_MJPEG_FPS_INSERT_AFTER [=[
    if (run_ret != ESP_PLAYER_ERR_OK) {
        return run_ret;
    }

    /*
     * Correct the known 1280x720 MJPEG AVI timing after the decoder has
     * reported stream info. Applying this before decoder startup is too early:
     * the decoder can overwrite track_info.video_info.fps with 10 again.
     * Keep the track metadata, sync clock, and renderer configuration aligned.
     */
    if (stream->video_side != NULL &&
        stream->video_side->track_info.video_info.width == 1280 &&
        stream->video_side->track_info.video_info.height == 720) {
        int32_t reported_fps = stream->video_side->track_info.video_info.fps;
        stream->video_side->track_info.video_info.fps = 25;
        if (!(stream->expected_tasks & TASK_STATUS_EXTRACTOR_RUNNING)) {
            player_sync_set_video_fps(stream->sync_handle, 25);
            player_sync_enable_video_fps_sync(stream->sync_handle, true);
        }
        ESP_LOGW(TAG,
                 "Correct MJPEG AVI FPS after decoder stream info: extractor=%d, using=25",
                 (int)reported_fps);
    }

    return ESP_PLAYER_ERR_OK;
}]=])

    string(FIND "${ESP_PLAYER_VIDEO_PIPELINE_SOURCE}" "${ESP_PLAYER_MJPEG_FPS_INSERT_BEFORE}" ESP_PLAYER_MJPEG_FPS_POSITION)
    if(ESP_PLAYER_MJPEG_FPS_POSITION EQUAL -1)
        message(FATAL_ERROR "ESP-Player video pipeline changed; cannot apply the post-decoder MJPEG FPS correction")
    endif()

    string(REPLACE "${ESP_PLAYER_MJPEG_FPS_INSERT_BEFORE}"
                   "${ESP_PLAYER_MJPEG_FPS_INSERT_AFTER}"
           ESP_PLAYER_VIDEO_PIPELINE_SOURCE "${ESP_PLAYER_VIDEO_PIPELINE_SOURCE}")
    file(WRITE "${ESP_PLAYER_VIDEO_PIPELINE}" "${ESP_PLAYER_VIDEO_PIPELINE_SOURCE}")
    message(STATUS "ESP-Player: corrected 1280x720 MJPEG AVI timing after decoder stream info")
else()
    message(STATUS "ESP-Player: post-decoder 25 FPS MJPEG AVI timing correction already present")
endif()
