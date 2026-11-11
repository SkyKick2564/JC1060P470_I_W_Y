# Pull OTA over the local LAN

OTA Manager runs as its own FreeRTOS task. It receives the existing
Network Manager handle, waits until `network_status_t.has_ip` is true, and
then periodically checks a manifest served by the development PC.

The device downloads the inactive application partition, verifies the
manifest SHA-256 and ESP application image, and only then selects the new
boot partition. An interrupted or invalid download does not replace the
currently running application.

## 1. Configure OTA in the application

Edit `main/app_config.h`:

- `manifest_url`: the PC's LAN IPv4 address and manifest path
- `auth_token`: the same 16-127 character token entered when starting the PC server
- `check_interval_sec`: check interval in seconds (10-3600)

These are application settings, not menuconfig options. The OTA Manager
component remains independent of the application's chosen URL, token, and
polling interval.

## 2. Firmware version is incremented automatically

When using **ESP-IDF: Build your Project** in VS Code, the pre-build hook
increments the last numeric version field in the project-root `version.txt`.

For example:

- `1.0.3.8` becomes `1.0.3.9`
- `1.0.3.9` becomes `1.0.3.10`

CMake is configured to re-read `version.txt`, so the new version is embedded
in the firmware built by that command. Do not edit `version.txt` manually
for normal builds.

The version is finalized only after the build task succeeds. If a build fails
or is interrupted, the next VS Code Build attempt restores the previous
version and retries the same next version. While a build is pending, the LAN
OTA server and GitHub publish script refuse to serve or publish the image.

This automatic version workflow is attached to the ESP-IDF VS Code extension
Build command. A direct `idf.py build` in a terminal does not run the VS Code
pre-build and post-build hooks.

## 3. Build and flash the initial firmware

Use **ESP-IDF: Build your Project** in VS Code. For the initial USB flash:

```powershell
idf.py -p COM7 flash monitor
```

## 4. Start the firmware server on the PC

After a successful build, run this from the project root:

```powershell
python tools/ota_server.py
```

The script serves `build/JC1060P470_I_W_Y.bin`, reads the matching version
from `version.txt`, calculates SHA-256, and exposes `/manifest.json` and
`/firmware.bin` on port 8080. Enter the same token configured in
`main/app_config.h`.

Keep the server running while the board checks for updates. Windows Firewall
may ask permission for Python to accept connections on the private network.
The PC and board must be on the same reachable LAN; Internet access is not
required. The token is sent over plain HTTP, so use this only on a trusted LAN
and do not expose port 8080 to the internet.

## Update workflow

1. Stop the PC server with Ctrl+C if it is running.
2. Run **ESP-IDF: Build your Project** in VS Code. The version increments automatically.
3. Wait for the build to finish successfully.
4. Restart `python tools/ota_server.py` for LAN OTA, or run
   `tools/publish_github_ota.ps1` to publish GitHub OTA.
5. The board detects the higher version on its next check and downloads it.

Do not change the version file while the server is running. Restart the server
after every successful build so it serves the new image and its matching manifest.

## Rollback protection

Rollback confirmation is owned by the OTA Manager. The application starts
Network Manager and completes its critical startup steps first, then calls
`ota_manager_start()`. At that point OTA Manager checks whether the running
image is `ESP_OTA_IMG_PENDING_VERIFY` and confirms it with ESP-IDF. If the
application reboots before reaching `ota_manager_start()`, the bootloader can
roll back to the previous firmware. Keep `ota_manager_start()` after all
critical application initialization (for example, display and touch startup
when those are added).
