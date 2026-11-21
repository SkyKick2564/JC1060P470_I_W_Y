#!/usr/bin/env python3
"""Apply temporary PPA timing instrumentation to the locked GMF Video component.

Run after idf.py reconfigure and before idf.py build:
    python tools/patch_gmf_video_ppa_profile.py
The script is idempotent and only edits managed_components.
"""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
CANDIDATES = [
    ROOT / "managed_components" / "espressif__gmf_video" / "src" / "esp_gmf_video_ppa.c",
    ROOT / "managed_components" / "espressif__gmf_video" / "elements" / "gmf_video" / "src" / "esp_gmf_video_ppa.c",
]
CANDIDATES.extend(ROOT.glob("managed_components/**/esp_gmf_video_ppa.c"))
TARGET = next((p for p in CANDIDATES if p.is_file()), None)
if TARGET is None:
    sys.exit("ERROR: esp_gmf_video_ppa.c not found. Run idf.py reconfigure first.")

source = TARGET.read_text(encoding="utf-8")
if "PPA_PROFILE input_wait" in source:
    # The C source may already be patched by an earlier run; still ensure the
    # component declares esp_timer so the include is available to this target.
    CMAKE_FILE = TARGET.parents[2] / "CMakeLists.txt"
    if not CMAKE_FILE.is_file():
        sys.exit(f"ERROR: component CMakeLists.txt not found: {CMAKE_FILE}")
    cmake_source = CMAKE_FILE.read_text(encoding="utf-8")
    if "list(APPEND COMPONENT_REQUIRES esp_timer)" not in cmake_source:
        anchor = "idf_component_register("
        if cmake_source.count(anchor) != 1:
            sys.exit("ERROR: expected exactly one idf_component_register() in component CMakeLists.txt.")
        cmake_source = cmake_source.replace(
            anchor,
            "# Required by temporary PPA profiling instrumentation.\n"
            "list(APPEND COMPONENT_REQUIRES esp_timer)\n\n" + anchor,
            1,
        )
        CMAKE_FILE.write_text(cmake_source, encoding="utf-8", newline="\n")
        print(f"Added esp_timer dependency in: {CMAKE_FILE}")
    print(f"PPA profiling already installed: {TARGET}")
    raise SystemExit(0)

def replace_once(old: str, new: str, label: str) -> None:
    global source
    count = source.count(old)
    if count != 1:
        sys.exit(f"ERROR: expected exactly one '{label}' anchor, found {count}. No file written.")
    source = source.replace(old, new, 1)

replace_once(
    '#include "esp_log.h"\n',
    '#include "esp_log.h"\n#include "esp_timer.h"\n',
    "esp_log include",
)
replace_once(
    'static const char *TAG = "PPA_EL";\n',
    '''static const char *TAG = "PPA_EL";

/* PPA_PROFILE: cumulative timings, reported every 50 converted frames. */
static uint32_t s_profile_frames;
static int64_t s_profile_input_wait_us;
static int64_t s_profile_output_wait_us;
static int64_t s_profile_convert_us;
static int64_t s_profile_lock_wait_us;
static int64_t s_profile_hw_transaction_us;
''',
    "profile counters",
)
replace_once(
    '''    ppa_srm_lock();
    int err = ppa_do_scale_rotate_mirror(vid_cvt->ppa_handle, &vid_cvt->ppa_config);
    ppa_srm_unlock();
    return err;
''',
    '''    int64_t lock_start_us = esp_timer_get_time();
    ppa_srm_lock();
    int64_t hw_start_us = esp_timer_get_time();
    int err = ppa_do_scale_rotate_mirror(vid_cvt->ppa_handle, &vid_cvt->ppa_config);
    int64_t hw_end_us = esp_timer_get_time();
    ppa_srm_unlock();
    s_profile_lock_wait_us += hw_start_us - lock_start_us;
    s_profile_hw_transaction_us += hw_end_us - hw_start_us;
    return err;
''',
    "PPA lock and transaction",
)
replace_once(
    '''    int ret = esp_gmf_port_acquire_in(in_port, &in_load, ESP_GMF_ELEMENT_GET(vid_cvt)->in_attr.data_size, in_wait_ticks);
    if (ret < 0) {
''',
    '''    int64_t input_wait_start_us = esp_timer_get_time();
    int ret = esp_gmf_port_acquire_in(in_port, &in_load, ESP_GMF_ELEMENT_GET(vid_cvt)->in_attr.data_size, in_wait_ticks);
    int64_t input_wait_us = esp_timer_get_time() - input_wait_start_us;
    if (ret < 0) {
''',
    "input acquire timing",
)
replace_once(
    '''    ret = esp_gmf_port_acquire_out(out_port, &out_load, wanted_size, ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, ret, ret, esp_gmf_port_release_in(in_port, in_load, ESP_GMF_MAX_DELAY); return ret);
    if (in_load->valid_size > 0 && bypass == false) {
''',
    '''    int64_t output_wait_start_us = esp_timer_get_time();
    ret = esp_gmf_port_acquire_out(out_port, &out_load, wanted_size, ESP_GMF_MAX_DELAY);
    int64_t output_wait_us = esp_timer_get_time() - output_wait_start_us;
    ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, ret, ret, esp_gmf_port_release_in(in_port, in_load, ESP_GMF_MAX_DELAY); return ret);
    if (in_load->valid_size > 0 && bypass == false) {
''',
    "output acquire timing",
)
replace_once(
    '''#if CONFIG_SOC_PPA_SUPPORTED
        if (vid_cvt->sw_imgfx_only || vid_cvt->use_ppa) {
            ret = video_ppa_run_convert(vid_cvt, in_load, out_load);
        } else {
            ret = dm2d_convert(vid_cvt, in_load, out_load);
        }
''',
    '''#if CONFIG_SOC_PPA_SUPPORTED
        int64_t convert_start_us = esp_timer_get_time();
        if (vid_cvt->sw_imgfx_only || vid_cvt->use_ppa) {
            ret = video_ppa_run_convert(vid_cvt, in_load, out_load);
        } else {
            ret = dm2d_convert(vid_cvt, in_load, out_load);
        }
        int64_t convert_us = esp_timer_get_time() - convert_start_us;
        s_profile_input_wait_us += input_wait_us;
        s_profile_output_wait_us += output_wait_us;
        s_profile_convert_us += convert_us;
        s_profile_frames++;
        if ((s_profile_frames % 50U) == 0U) {
            ESP_LOGI(TAG,
                     "PPA_PROFILE input_wait=%.2fms output_wait=%.2fms convert=%.2fms lock_wait=%.2fms hw_transaction=%.2fms frames=%u",
                     (double)s_profile_input_wait_us / (double)s_profile_frames / 1000.0,
                     (double)s_profile_output_wait_us / (double)s_profile_frames / 1000.0,
                     (double)s_profile_convert_us / (double)s_profile_frames / 1000.0,
                     (double)s_profile_lock_wait_us / (double)s_profile_frames / 1000.0,
                     (double)s_profile_hw_transaction_us / (double)s_profile_frames / 1000.0,
                     (unsigned)s_profile_frames);
        }
''',
    "per-frame profile report",
)

# esp_timer is a public dependency of this component because the profiling
# code calls esp_timer_get_time() from esp_gmf_video_ppa.c.
CMAKE_FILE = TARGET.parents[2] / "CMakeLists.txt"
if not CMAKE_FILE.is_file():
    sys.exit(f"ERROR: component CMakeLists.txt not found: {CMAKE_FILE}. No file written.")

cmake_source = CMAKE_FILE.read_text(encoding="utf-8")
if "list(APPEND COMPONENT_REQUIRES esp_timer)" not in cmake_source:
    anchor = "idf_component_register("
    if cmake_source.count(anchor) != 1:
        sys.exit("ERROR: expected exactly one idf_component_register() in component CMakeLists.txt. No file written.")
    cmake_source = cmake_source.replace(
        anchor,
        "# Required by temporary PPA profiling instrumentation.\n"
        "list(APPEND COMPONENT_REQUIRES esp_timer)\n\n"
        + anchor,
        1,
    )

TARGET.write_text(source, encoding="utf-8", newline="\n")
CMAKE_FILE.write_text(cmake_source, encoding="utf-8", newline="\n")
print(f"Installed PPA profiling in: {TARGET}")
print(f"Added esp_timer dependency in: {CMAKE_FILE}")
print("Rebuild required. Search serial logs for: PPA_PROFILE")
