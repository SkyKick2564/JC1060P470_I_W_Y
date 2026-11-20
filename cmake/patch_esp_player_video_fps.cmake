# ESP-Player 1.0.7 currently copies the MP4 track FPS (25) into the
# render stream, bypassing esp_video_render_cfg_t.fps for the single-stream
# path. Override that stream FPS so the renderer rate controller is tested
# at the requested limit. Fail loudly if upstream changes the expected code.
set(ESP_PLAYER_VIDEO_PIPELINE
    "${CMAKE_CURRENT_LIST_DIR}/managed_components/espressif__esp_player/src/video/player_pipeline_video.c")

if(NOT EXISTS "${ESP_PLAYER_VIDEO_PIPELINE")
    message(FATAL_ERROR "ESP-Player source not found: ${ESP_PLAYER_VIDEO_PIPELINE")
endif()

file(READ "${ESP_PLAYER_VIDEO_PIPELINE" ESP_PLAYER_VIDEO_PIPELINE_SOURCE)
set(ESP_PLAYER_VIDEO_FPS_SOURCE "video_render_cfg.fps = stream->video_side->track_info.video_info.fps;")
set(ESP_PLAYER_VIDEO_FPS_OVERRIDE "video_render_cfg.fps = 10;")

string(FIND "${ESP_PLAYER_VIDEO_PIPELINE_SOURCE" "${ESP_PLAYER_VIDEO_FPS_SOURCE" ESP_PLAYER_VIDEO_FPS_POSITION)
if(ESP_PLAYER_VIDEO_FPS_POSITION EQUAL -1)
    message(FATAL_ERROR "ESP-Player video FPS assignment changed; review the 10 FPS override")
endif()

string(REPLACE "${ESP_PLAYER_VIDEO_FPS_SOURCE" "${ESP_PLAYER_VIDEO_FPS_OVERRIDE"
       ESP_PLAYER_VIDEO_PIPELINE_SOURCE "${ESP_PLAYER_VIDEO_PIPELINE_SOURCE")
file(WRITE "${ESP_PLAYER_VIDEO_PIPELINE" "${ESP_PLAYER_VIDEO_PIPELINE_SOURCE")
message(STATUS "ESP-Player render stream FPS overridden to 10 for playback test")
