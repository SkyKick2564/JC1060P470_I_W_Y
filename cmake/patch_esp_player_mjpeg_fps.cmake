set(ESP_PLAYER_VIDEO_PIPELINE
    "${CMAKE_CURRENT_LIST_DIR}/../managed_components/espressif__esp_player/src/video/player_pipeline_video.c")

if(NOT EXISTS "${ESP_PLAYER_VIDEO_PIPELINE}")
    message(FATAL_ERROR "ESP-Player source not found: ${ESP_PLAYER_VIDEO_PIPELINE}")
endif()

file(READ "${ESP_PLAYER_VIDEO_PIPELINE}" ESP_PLAYER_VIDEO_PIPELINE_SOURCE)

set(ESP_PLAYER_MJPEG_FPS_NEEDLE [=[
esp_player_err_t player_pl_run_create_video_decoder(esp_player_stream_t *stream)
{
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();]=])

set(ESP_PLAYER_MJPEG_FPS_REPLACEMENT [=[
esp_player_err_t player_pl_run_create_video_decoder(esp_player_stream_t *stream)
{
    /*
     * The current AVI extractor reports 10 FPS for this 1280x720 MJPEG
     * source even though its AVI stream header is 25 FPS. Correct only this
     * known metadata mismatch before both A/V sync and renderer setup.
     */
    if (stream != NULL && stream->video_side != NULL &&
        stream->video_side->track_info.video_info.format == ESP_FOURCC_MJPG &&
        stream->video_side->track_info.video_info.width == 1280 &&
        stream->video_side->track_info.video_info.height == 720 &&
        stream->video_side->track_info.video_info.fps == 10) {
        ESP_LOGW(TAG, "Correcting MJPEG AVI FPS metadata from 10 to 25");
        stream->video_side->track_info.video_info.fps = 25;
    }

    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();]=])

string(FIND "${ESP_PLAYER_VIDEO_PIPELINE_SOURCE}" "${ESP_PLAYER_MJPEG_FPS_NEEDLE}" ESP_PLAYER_MJPEG_FPS_POSITION)
if(NOT ESP_PLAYER_MJPEG_FPS_POSITION EQUAL -1)
    string(REPLACE "${ESP_PLAYER_MJPEG_FPS_NEEDLE}" "${ESP_PLAYER_MJPEG_FPS_REPLACEMENT}"
           ESP_PLAYER_VIDEO_PIPELINE_SOURCE "${ESP_PLAYER_VIDEO_PIPELINE_SOURCE}")
    file(WRITE "${ESP_PLAYER_VIDEO_PIPELINE}" "${ESP_PLAYER_VIDEO_PIPELINE_SOURCE}")
    message(STATUS "ESP-Player: enabled 25 FPS correction for 1280x720 MJPEG AVI metadata")
else()
    string(FIND "${ESP_PLAYER_VIDEO_PIPELINE_SOURCE}" "Correcting MJPEG AVI FPS metadata from 10 to 25" ESP_PLAYER_MJPEG_FPS_PATCHED)
    if(ESP_PLAYER_MJPEG_FPS_PATCHED EQUAL -1)
        message(FATAL_ERROR "ESP-Player video pipeline changed; review the MJPEG FPS correction")
    endif()
endif()
