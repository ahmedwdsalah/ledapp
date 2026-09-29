# Motif display protocol, version 1

Hardware: Spotpear/Waveshare ESP32-S3-Touch-LCD-2.8C, 480 × 480 ST7701, 8 MB PSRAM, 16 MB flash. The first firmware installation uses USB download mode. Later animation uploads use Bluetooth for pairing and Wi-Fi for file transfer.

## Bluetooth LE

The display advertises `MOTIF-` followed by its last three MAC bytes and the service UUID `c4683809-f301-4312-83bb-31901d0245c7`.

| Characteristic | UUID | Value |
| --- | --- | --- |
| Info | `6edddece-b1f3-4cfb-9f64-127e68a2b9d3` | 16 bytes: protocol version (1), Wi-Fi state (1), IPv4 address (4, network order; zeros when offline), MAC (6), max animation bytes (4, little-endian) |
| Provision | `3eae6aab-3e28-4768-b019-affe73047fd7` | Encrypted write with response. Start `[1, length_lo, length_hi]`, data `[2, offset_lo, offset_hi, ...bytes]`, commit `[3]`. Payload is `[ssid_length, password_length, ...ssid, ...password]`. The display requires authenticated BLE pairing. |
| Upload key | `692d6042-d8fa-498b-a4db-8a7401d47baf` | Encrypted read: 16 random bytes, hex-encoded by the app for HTTP authorization. |

The display shows a fresh six-digit BLE passkey during pairing. It stores Wi-Fi credentials, BLE bond keys, and the upload key in NVS. Bond keys survive reboots; a one-time migration to persistent bonding rotates the BLE identity so phones can pair afresh. The app stores the upload key and last known IP in the device keychain/keystore. Status checks and uploads use authenticated local HTTP first; if the cached IP stops responding, the app reconnects over BLE to read the current address from Info.

## Local HTTP

The display listens on port 8080 on its Wi-Fi station address. The app and display must be on the same network. Every endpoint requires `Authorization: Bearer <32 lowercase hex digits>`.

| Request | Result |
| --- | --- |
| `GET /v1/status` | JSON: `protocol`, `deviceId`, `wifiConnected`, `generation`, `state` (`idle`, `applying`, `playing`, or `error`) |
| `GET /v1/diagnostics` | JSON: last 32 timestamped board events for Wi-Fi, BLE pairing, upload progress, and playback. Requires the same bearer token; does not include credentials. |
| `GET /v1/color-test` | Debug aid: `pattern=0..23` draws a built-in test pattern (`24` resumes playback), `profile=0..3` switches the runtime color profile (0 identity, 1 contrast, 2 gamma lift, 3 both). |
| `POST /v1/animation` | Raw `application/x-motif-animation` body. Maximum 4 MiB. Streams to `/media/current.motif.part` in internal flash, validates the header, and schedules replacement of `/media/current.motif`. Returns `202` with a generation. |

An animation starts with `MOTF`, version `1`, flags (bit 0 set: difference frames; bit 1 set: split streams), little-endian width and height `480`, and a little-endian frame count (`1–255`). Each frame contains a little-endian duration in milliseconds (`20–1000`), compressed byte count, and one or two zlib-compressed 480 × 480 RGB565 frames. With the difference flag, every frame after the first stores its pixels XORed against the previous decoded frame, and frame `0` of each loop pass is stored absolute. With the split flag, each frame carries a little-endian 4-byte length followed by two independent zlib streams covering the top and bottom halves, so the main task and a worker task on the second core inflate both halves in parallel. The board loads the stream into PSRAM, decompresses directly into the panel's own RGB565 frame buffers, applies the runtime color profile, and switches the scanout to the new buffer at the next frame boundary — no scaling and no mid-scan copy. The app polls status until the returned generation is playing. A failed transfer keeps the previous animation. No microSD card is required.

The plain HTTP transport is confined to the user's Wi-Fi network. BLE pairing protects Wi-Fi credentials and the upload key during provisioning; TLS or application-layer content encryption should be added before treating shared or untrusted LANs as supported environments.
