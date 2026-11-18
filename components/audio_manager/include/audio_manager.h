#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize Audio Manager after esp_board_manager_init().
 *
 * The Board Manager must already have initialized the "audio_dac" device.
 */
esp_err_t audio_manager_init(void);

/**
 * @brief Set speaker output volume.
 *
 * @param volume Output volume from 0.0 to 100.0.
 */
esp_err_t audio_manager_set_volume(float volume);

/**
 * @brief Get the currently configured output volume.
 *
 * @param volume Pointer receiving the volume from 0.0 to 100.0.
 */
esp_err_t audio_manager_get_volume(float *volume);

/**
 * @brief Decode and play an MP3 file through the board audio DAC.
 *
 * This first implementation is synchronous: the function returns after
 * playback finishes or an error occurs. The path may point to the SD card,
 * for example "/sdcard/music/song.mp3".
 *
 * @param path Absolute path to an MP3 file.
 */
esp_err_t audio_manager_play_mp3(const char *path);

/**
 * @brief Check whether Audio Manager has been initialized.
 */
bool audio_manager_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_MANAGER_H */
