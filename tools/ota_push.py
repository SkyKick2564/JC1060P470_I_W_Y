#!/usr/bin/env python3
"""Send an ESP-IDF application image to the board's local HTTP OTA endpoint."""

import argparse
import getpass
import sys
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


DEFAULT_FIRMWARE = Path("build/JC1060P470_I_W_Y.bin")
OTA_PORT = 3232


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Upload a built ESP-IDF firmware image to an ESP32 OTA endpoint."
    )
    parser.add_argument("ip", help="IP address printed by Network Manager")
    parser.add_argument(
        "--firmware",
        type=Path,
        default=DEFAULT_FIRMWARE,
        help=f"Application .bin file (default: {DEFAULT_FIRMWARE})",
    )
    args = parser.parse_args()

    if not args.firmware.is_file():
        print(f"Firmware file not found: {args.firmware}", file=sys.stderr)
        return 2

    token = getpass.getpass("OTA authentication token: ")
    if len(token) < 16:
        print("The OTA token must contain at least 16 characters.", file=sys.stderr)
        return 2

    image = args.firmware.read_bytes()
    if not image:
        print("Firmware file is empty.", file=sys.stderr)
        return 2

    url = f"http://{args.ip}:{OTA_PORT}/update"
    request = Request(
        url,
        data=image,
        headers={
            "Content-Type": "application/octet-stream",
            "X-OTA-Token": token,
        },
        method="POST",
    )

    print(f"Uploading {args.firmware} ({len(image)} bytes) to {url} ...")
    try:
        with urlopen(request, timeout=180) as response:
            print(response.read().decode("utf-8", errors="replace"))
            return 0
    except HTTPError as exc:
        print(f"OTA rejected: HTTP {exc.code}", file=sys.stderr)
        print(exc.read().decode("utf-8", errors="replace"), file=sys.stderr)
        return 1
    except (URLError, TimeoutError, OSError) as exc:
        print(f"OTA connection failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
