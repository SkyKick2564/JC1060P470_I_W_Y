#ifndef MEDIA_MANAGER_H264_OPENH264_H
#define MEDIA_MANAGER_H264_OPENH264_H

#include <stddef.h>\n#include <stdint.h>\n#include "esp_video_codec_types.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_vc_err_t media_manager_h264_openh264_register(void);\nesp_vc_err_t media_manager_h264_openh264_set_avcc(const uint8_t *record, size_t size);

#ifdef __cplusplus
}
#endif

#endif
