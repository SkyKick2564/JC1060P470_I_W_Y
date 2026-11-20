set(ESP_PLAYER_VIDEO_PIPELINE
    "${CMAKE_CURRENT_LIST_DIR}/../managed_components/espressif__esp_player/src/video/player_pipeline_video.c")

if(NOT EXISTS "${ESP_PLAYER_VIDEO_PIPELINE}")
    message(FATAL_ERROR "ESP-Player source not found: ${ESP_PLAYER_VIDEO_PIPELINE}")
endif()

file(READ "${ESP_PLAYER_VIDEO_PIPELINE}" ESP_PLAYER_VIDEO_PIPELINE_SOURCE)

# The AVI extractor reports 10 FPS for this known 1280x720, 25 FPS MJPEG file.
# Override the timing unconditionally for this resolution before ESP-Player
# configures its video sync clock and renderer. Do not depend on the extractor's
# codec tag or the incorrect FPS value matching a particular representation.
set(ESP_PLAYER_MJPEG_FPS_MARKER "Force MJPEG AVI renderer timing to 25 FPS")

string(FIND "${ESP_PLAYER_VIDEO_PIPELINE_SOURCE}" "${ESP_PLAYER_MJPEG_FPS_MARKER}" ESP_PLAYER_MJPEG_FPS_PATCHED)
if(ESP_PLAYER_MJPEG_FPS_PATCHED EQUAL -1)
    set(ESP_PLAYER_MJPEG_FPS_INSERT_BEFORE [=[
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();]=])

    set(ESP_PLAYER_MJPEG_FPS_INSERT_AFTER [=[
    /*
     * This board's 1280x720 MJPEG AVI is encoded at 25 FPS, but the AVI
     * extractor reports 10 FPS. Correct the stream timing before ESP-Player
     * configures both the A/V sync clock and video renderer. Restrict this
     * workaround to the known test resolution.
     */
    if (stream != NULL && stream->video_side != NULL &&
        stream->video_side->track_info.video_info.width == 1280 &&
        stream->video_side->track_info.video_info.height == 720) {
        int32_t reported_fps = stream->video_side->track_info.video_info.fps;
        stream->video_side->track_info.video_info.fps = 25;
        ESP_LOGW(TAG,
                 "${ESP_PLAYER_MJPEG_FPS_MARKER}: extractor=%" PRId32 ", using=25",
                 reported_fps);
    }

    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();]=])

    string(FIND "${ESP_PLAYER_VIDEO_PIPELINE_SOURCE}" "${ESP_PLAYER_MJPEG_FPS_INSERT_BEFORE}" ESP_PLAYER_MJPEG_FPS_POSITION)
    if(ESP_PLAYER_MJPEG_FPS_POSITION EQUAL -1)
        message(FATAL_ERROR "ESP-Player video pipeline changed; cannot apply the MJPEG FPS correction")
    endif()

    string(REPLACE "${ESP_PLAYER_MJPEG_FPS_INSERT_BEFORE}"
                   "${ESP_PLAYER_MJPEG_FPS_INSERT_AFTER}"
           ESP_PLAYER_VIDEO_PIPELINE_SOURCE "${ESP_PLAYER_VIDEO_PIPELINE_SOURCE}")
    file(WRITE "${ESP_PLAYER_VIDEO_PIPELINE}" "${ESP_PLAYER_VIDEO_PIPELINE_SOURCE}")
    message(STATUS "ESP-Player: forced 1280x720 MJPEG AVI timing to 25 FPS")
else()
    message(STATUS "ESP-Player: 25 FPS MJPEG AVI timing correction already present")
endif()
