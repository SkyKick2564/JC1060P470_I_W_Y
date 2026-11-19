#ifndef MEDIA_MANAGER_H
#define MEDIA_MANAGER_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MEDIA_MANAGER_PATH_MAX 256

typedef enum {
    MEDIA_MANAGER_STATE_UNINITIALIZED = 0,
    MEDIA_MANAGER_STATE_IDLE,
    MEDIA_MANAGER_STATE_QUEUED,
    MEDIA_MANAGER_STATE_PLAYING,
    MEDIA_MANAGER_STATE_PAUSED,
    MEDIA_MANAGER_STATE_STOPPING,
    MEDIA_MANAGER_STATE_STOPPED,
    MEDIA_MANAGER_STATE_FINISHED,
    MEDIA_MANAGER_STATE_ERROR
} media_manager_state_t;

typedef enum {
    MEDIA_MANAGER_EVENT_STARTED = 0,
    MEDIA_MANAGER_EVENT_PAUSED,
    MEDIA_MANAGER_EVENT_RESUMED,
    MEDIA_MANAGER_EVENT_STOPPED,
    MEDIA_MANAGER_EVENT_FINISHED,
    MEDIA_MANAGER_EVENT_ERROR
} media_manager_event_t;

typedef void (*media_manager_event_callback_t)(
    media_manager_event_t event,
    esp_err_t error,
    const char *path,
    void *user_ctx
);

/**
 * @brief Initialize Media Manager after esp_board_manager_init().
 *
 * Creates the background playback task and obtains the Board Manager
 * "audio_dac" device. Audio and video playback use Espressif decoder/render
 * components; MP3/WAV playback retains the existing audio decoder path.
 */
esp_err_t media_manager_init(void);

/**
 * @brief Deinitialize Media Manager. Playback must be idle or completed.
 */
esp_err_t media_manager_deinit(void);

/**
 * @brief Register an optional playback event callback.
 *
 * The callback runs in the Media Manager playback task. Keep it short and
 * do not perform blocking playback operations from the callback.
 */
esp_err_t media_manager_register_event_callback(
    media_manager_event_callback_t callback,
    void *user_ctx
);

/**
 * @brief Optionally set the EEZ/LVGL Image widget for decoded video frames.
 *
 * Call after app_init()/ui_init(), when the EEZ Image widget exists. Pass NULL
 * to detach the target. Without a registered target, video containers are
 * played in audio-only mode. The widget must remain alive until video playback
 * has stopped and the video renderer is destroyed.
 *
 * Configure the EEZ Image widget's image alignment to Stretch if the video
 * should fill the widget while preserving the widget's layout and position.
 * This function does not create or modify the UI layout.
 */
esp_err_t media_manager_set_video_widget(lv_obj_t *image_widget);

/**
 * @brief Queue a file from the SD card for playback and return immediately.
 *
 * Audio: .wav and .mp3.
 * Video containers: .mp4, .avi and .ts. With a registered video widget,
 * audio/video tracks are played together; without one, only the audio track
 * is played.
 * Example: "/sdcard/media/movie.mp4".
 */
esp_err_t media_manager_play_file(const char *path);

/** Pause the current playback. */
esp_err_t media_manager_pause(void);

/** Resume a paused playback. */
esp_err_t media_manager_resume(void);

/** Stop queued or active playback. */
esp_err_t media_manager_stop(void);

/** Set speaker output volume in the range 0.0 to 100.0. */
esp_err_t media_manager_set_volume(float volume);

/** Get the configured speaker output volume. */
esp_err_t media_manager_get_volume(float *volume);

/** Get the current playback state. */
esp_err_t media_manager_get_state(media_manager_state_t *state);

/** Copy the path of the current or most recently queued file. */
esp_err_t media_manager_get_current_file(char *path, size_t path_size);

/** Check whether Media Manager has been initialized. */
bool media_manager_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* MEDIA_MANAGER_H */
