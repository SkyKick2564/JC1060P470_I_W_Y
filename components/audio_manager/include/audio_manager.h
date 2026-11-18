#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_MANAGER_PATH_MAX 256

typedef enum {
    AUDIO_MANAGER_STATE_UNINITIALIZED = 0,
    AUDIO_MANAGER_STATE_IDLE,
    AUDIO_MANAGER_STATE_QUEUED,
    AUDIO_MANAGER_STATE_PLAYING,
    AUDIO_MANAGER_STATE_PAUSED,
    AUDIO_MANAGER_STATE_STOPPING,
    AUDIO_MANAGER_STATE_STOPPED,
    AUDIO_MANAGER_STATE_FINISHED,
    AUDIO_MANAGER_STATE_ERROR
} audio_manager_state_t;

typedef enum {
    AUDIO_MANAGER_EVENT_STARTED = 0,
    AUDIO_MANAGER_EVENT_PAUSED,
    AUDIO_MANAGER_EVENT_RESUMED,
    AUDIO_MANAGER_EVENT_STOPPED,
    AUDIO_MANAGER_EVENT_FINISHED,
    AUDIO_MANAGER_EVENT_ERROR
} audio_manager_event_t;

typedef void (*audio_manager_event_callback_t)(
    audio_manager_event_t event,
    esp_err_t error,
    const char *path,
    void *user_ctx
);

/**
 * @brief Initialize Audio Manager after esp_board_manager_init().
 *
 * Creates the background playback task and obtains the Board Manager
 * "audio_dac" device. WAV and MP3 decoders are provided by esp_audio_codec.
 */
esp_err_t audio_manager_init(void);

/**
 * @brief Deinitialize Audio Manager. Playback must be idle or completed.
 */
esp_err_t audio_manager_deinit(void);

/**
 * @brief Register an optional playback event callback.
 *
 * The callback runs in the Audio Manager playback task. Keep it short and
 * do not perform blocking playback operations from the callback.
 */
esp_err_t audio_manager_register_event_callback(
    audio_manager_event_callback_t callback,
    void *user_ctx
);

/**
 * @brief Queue a WAV or MP3 file for playback and return immediately.
 *
 * Supported extensions are .wav and .mp3 (case-insensitive). Example:
 * "/sdcard/music/song.mp3".
 */
esp_err_t audio_manager_play_file(const char *path);

/** Pause the current playback. */
esp_err_t audio_manager_pause(void);

/** Resume a paused playback. */
esp_err_t audio_manager_resume(void);

/** Stop queued or active playback. */
esp_err_t audio_manager_stop(void);

/** Set speaker output volume in the range 0.0 to 100.0. */
esp_err_t audio_manager_set_volume(float volume);

/** Get the configured speaker output volume. */
esp_err_t audio_manager_get_volume(float *volume);

/** Get the current playback state. */
esp_err_t audio_manager_get_state(audio_manager_state_t *state);

/** Copy the path of the current or most recently queued file. */
esp_err_t audio_manager_get_current_file(char *path, size_t path_size);

/** Check whether Audio Manager has been initialized. */
bool audio_manager_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_MANAGER_H */
