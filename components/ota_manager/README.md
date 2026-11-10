# Local LAN OTA

The OTA manager starts its HTTP endpoint after Network Manager reports
`NETWORK_EVENT_GOT_IP`. It does not initialize or control Wi-Fi itself.

## Configure the upload token

Run:

```powershell
idf.py menuconfig
```

Open **OTA Manager** and set **OTA upload authentication token** to a random
secret containing 16 to 127 characters. The token is compiled into the
firmware, so keep it private and do not commit a real token to Git.

The endpoint uses plain HTTP on the trusted local network. The token is not
encrypted in transit; do not expose port 3232 to the internet or use this
endpoint on an untrusted network.

## Build and upload

Build the project and flash this initial firmware over USB:

```powershell
idf.py build
idf.py -p COM7 flash monitor
```

After Wi-Fi connects, the log reports the board IP and that the OTA service is
ready. From the project root on the PC, run:

```powershell
python tools/ota_push.py <BOARD_IP>
```

The script asks for the token and uploads
`build/JC1060P470_I_W_Y.bin` to `http://<BOARD_IP>:3232/update`.

Only the application image is uploaded. Do not send the bootloader or
partition-table binaries to this endpoint.

The device writes to the inactive OTA app partition. It selects that partition
for the next boot only after the complete image has been received and validated.
If the transfer is interrupted or the image is invalid, the current firmware
remains selected. Application rollback is enabled in the project configuration.
