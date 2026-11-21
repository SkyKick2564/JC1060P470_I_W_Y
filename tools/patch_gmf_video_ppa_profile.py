#!/usr/bin/env python3
"""Update the temporary PPA timing instrumentation to use esp_log_timestamp()."""
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
if "PPA_PROFILE input_wait" not in source:
    sys.exit("ERROR: PPA profiling instrumentation is not installed; refusing to modify source.")

# esp_log_timestamp() is already available through esp_log.h and avoids
# changing the managed component's CMakeLists.txt/dependency declaration.
source = source.replace('#include "esp_timer.h"\n', "")
source = source.replace("esp_timer_get_time()", "ppa_profile_time_us()")

if "static int64_t ppa_profile_time_us(void)" not in source:
    anchor = 'static const char *TAG = "PPA_EL";\n'
    if source.count(anchor) != 1:
        sys.exit("ERROR: cannot locate PPA profiling helper insertion point. No file written.")
    helper = (
        "\n/* Millisecond-resolution clock; sufficient for frame-level profiling. */\n"
        "static int64_t ppa_profile_time_us(void)\n"
        "{\n"
        "    return (int64_t)esp_log_timestamp() * 1000LL;\n"
        "}\n"
    )
    source = source.replace(anchor, anchor + helper, 1)

TARGET.write_text(source, encoding="utf-8", newline="\n")
print(f"Updated PPA profiling in: {TARGET}")
print("Clock: esp_log_timestamp() (1 ms resolution); no CMake dependency change required.")
print("Rebuild required. Search serial logs for: PPA_PROFILE")
