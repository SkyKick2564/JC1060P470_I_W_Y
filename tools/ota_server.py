#!/usr/bin/env python3
"""Serve the current ESP-IDF application image and OTA manifest on the local LAN."""

import argparse
import getpass
import hashlib
import json
import re
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse


PROJECT_NAME = "JC1060P470_I_W_Y"
DEFAULT_FIRMWARE = Path("build/JC1060P470P470_I_W_Y.bin")
DEFAULT_VERSION_FILE = Path("version.txt")
DEFAULT_PORT = 8080
TOKEN_HEADER = "X-OTA-Token"


def parse_version(value: str) -> tuple[int, ...]:
    if not re.fullmatch(r"\d+(?:\.\d+){1,3}", value):
        raise argparse.ArgumentTypeError(
            "Version must contain 2-4 numeric parts, for example 1.0.1"
        )
    return tuple(int(part) for part in value.split("."))


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as firmware:
        for chunk in iter(lambda: firmware.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


class OTARequestHandler(BaseHTTPRequestHandler):
    firmware_path: Path
    firmware_size: int
    firmware_sha256: str
    firmware_version: str
    auth_token: str

    def _authorized(self) -> bool:
        return self.headers.get(TOKEN_HEADER, "") == self.auth_token

    def _send_bytes(self, status: int, content_type: str, body: bytes) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        path = urlparse(self.path).path

        if path not in ("/manifest.json", "/firmware.bin"):
            self._send_bytes(404, "text/plain; charset=utf-8", b"Not found")
            return

        if not self._authorized():
            self._send_bytes(401, "text/plain; charset=utf-8", b"Unauthorized")
            return

        if path == "/manifest.json":
            host = self.headers.get("Host")
            if not host:
                self._send_bytes(400, "text/plain; charset=utf-8", b"Missing Host header")
                return

            manifest = {
                "project": PROJECT_NAME,
                "version": self.firmware_version,
                "firmware_url": f"http://{host}/firmware.bin",
                "size": self.firmware_size,
                "sha256": self.firmware_sha256,
            }
            body = json.dumps(manifest, separators=(",", ":")).encode("utf-8")
            self._send_bytes(200, "application/json", body)
            return

        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(self.firmware_size))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()

        with self.firmware_path.open("rb") as firmware:
            while chunk := firmware.read(64 * 1024):
                self.wfile.write(chunk)

    def log_message(self, fmt: str, *args: object) -> None:
        print(f"[OTA Server] {self.address_string()} - {fmt % args}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Serve an ESP-IDF application image for local pull OTA."
    )
    parser.add_argument(
        "--firmware",
        type=Path,
        default=DEFAULT_FIRMWARE,
        help=f"Application .bin file (default: {DEFAULT_FIRMWARE})",
    )
    parser.add_argument(
        "--version-file",
        type=Path,
        default=DEFAULT_VERSION_FILE,
        help=f"Version file embedded into the firmware (default: {DEFAULT_VERSION_FILE})",
    )
    parser.add_argument("--host", default="0.0.0.0", help="Bind address (default: all interfaces)")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    args = parser.parse_args()

    firmware_path = args.firmware.resolve()
    version_file = args.version_file.resolve()

    if not firmware_path.is_file():
        parser.error(f"Firmware file not found: {firmware_path}")
    if not version_file.is_file():
        parser.error(f"Version file not found: {version_file}")

    firmware_size = firmware_path.stat().st_size
    if firmware_size == 0:
        parser.error("Firmware file is empty")

    try:
        version_text = version_file.read_text(encoding="utf-8").strip()
        version_parts = parse_version(version_text)
    except (OSError, argparse.ArgumentTypeError) as exc:
        parser.error(str(exc))

    token = getpass.getpass("OTA authentication token: ")
    if not 16 <= len(token) <= 127:
        parser.error("The OTA token must contain 16 to 127 characters")

    handler = type(
        "ConfiguredOTARequestHandler",
        (OTARequestHandler,),
        {
            "firmware_path": firmware_path,
            "firmware_size": firmware_size,
            "firmware_sha256": sha256_file(firmware_path),
            "firmware_version": ".".join(str(part) for part in version_parts),
            "auth_token": token,
        },
    )

    server = ThreadingHTTPServer((args.host, args.port), handler)
    print(f"Firmware : {firmware_path}")
    print(f"Version  : {handler.firmware_version}")
    print(f"Size     : {firmware_size} bytes")
    print(f"SHA-256  : {handler.firmware_sha256}")
    print(f"Listening: http://{args.host}:{args.port}")
    print("Manifest : /manifest.json")
    print("Firmware : /firmware.bin")
    print("Press Ctrl+C to stop.")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping OTA server...")
    finally:
        server.server_close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
