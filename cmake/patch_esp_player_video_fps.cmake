# ESP-Player 1.0.7 copies the MP4 track FPS (25) into the render stream,
# bypassing esp_video_render_cfg_t.fps on this single-stream path. Override
# the stream FPS for the requested 10 FPS renderer test.
set(ESP_PLAYER_VIDEO_PIPELINE
    "${CMAKE_CURRENT_LIST_DIR}/../managed_components/espressif__esp_player/src/video/player_pipeline_video.c")

if(NOT EXISTS "${ESP_PLAYER_VIDEO_PIPELINE}")
    message(FATAL_ERROR "ESP-Player source not found: ${ESP_PLAYER_VIDEO_PIPELINE}")
endif()

file(READ "${ESP_PLAYER_VIDEO_PIPELINE}" ESP_PLAYER_VIDEO_PIPELINE_SOURCE)
set(ESP_PLAYER_VIDEO_FPS_SOURCE "video_render_cfg.fps = stream->video_side->track_info.video_info.fps;")
set(ESP_PLAYER_VIDEO_FPS_OVERRIDE "video_render_cfg.fps = 10;")

string(FIND "${ESP_PLAYER_VIDEO_PIPELINE_SOURCE}" "${ESP_PLAYER_VIDEO_FPS_SOURCE}" ESP_PLAYER_VIDEO_FPS_POSITION)
if(NOT ESP_PLAYER_VIDEO_FPS_POSITION EQUAL -1)
    string(REPLACE "${ESP_PLAYER_VIDEO_FPS_SOURCE}" "${ESP_PLAYER_VIDEO_FPS_OVERRIDE}"
           ESP_PLAYER_VIDEO_PIPELINE_SOURCE "${ESP_PLAYER_VIDEO_PIPELINE_SOURCE}")
    file(WRITE "${ESP_PLAYER_VIDEO_PIPELINE}" "${ESP_PLAYER_VIDEO_PIPELINE_SOURCE}")
    message(STATUS "ESP-Player render stream FPS overridden to 10 for playback test")
else()
    string(FIND "${ESP_PLAYER_VIDEO_PIPELINE_SOURCE}" "${ESP_PLAYER_VIDEO_FPS_OVERRIDE}" ESP_PLAYER_VIDEO_OVERRIDE_POSITION)
    if(ESP_PLAYER_VIDEO_OVERRIDE_POSITION EQUAL -1)
        message(FATAL_ERROR "ESP-Player video FPS assignment changed; review the 10 FPS override")
    endif()
    message(STATUS "ESP-Player 10 FPS render override already applied")
endif()
