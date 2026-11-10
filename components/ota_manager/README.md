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

## 2. Set the firmware version

Edit the project-root `version.txt` before building. The same version is
embedded in the application descriptor and read by the PC server.

For example, change `1.0.0` to `1.0.1` for the next firmware build.

## 3. Build and flash the initial firmware

```powershell
idf.py build
idf.py -p COM7 flash monitor
```

## 4. Start the firmware server on the PC

After building, run this from the project root:

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

1. Stop the PC server with Ctrl+C.
2. Increase `version.txt`.
3. Build with `idf.py build`.
4. Start `python tools/ota_server.py` again.
5. The board detects the higher version on its next check and downloads it.

Do not change the version file while the server is running. Restart the server
after every build so it serves the new image and its matching manifest.
