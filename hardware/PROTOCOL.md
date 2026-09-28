# Motif display protocol, version 1

Hardware: Spotpear/Waveshare ESP32-S3-Touch-LCD-2.8C, 480 × 480 ST7701, 8 MB PSRAM, 16 MB flash. The first firmware installation uses USB download mode. Later animation uploads use Bluetooth for pairing and Wi-Fi for file transfer.

## Bluetooth LE

The display advertises `MOTIF-` followed by its last three MAC bytes and the service UUID `c4683809-f301-4312-83bb-31901d0245c7`.

| Characteristic | UUID | Value |
| --- | --- | --- |
| Info | `6edddece-b1f3-4cfb-9f64-127e68a2b9d3` | 16 bytes: protocol version (1), Wi-Fi state (1), IPv4 address (4, network order; zeros when offline), MAC (6), max GIF bytes (4, little-endian) |
| Provision | `3eae6aab-3e28-4768-b019-affe73047fd7` | Encrypted write with response. Start `[1, length_lo, length_hi]`, data `[2, offset_lo, offset_hi, ...bytes]`, commit `[3]`. Payload is `[ssid_length, password_length, ...ssid, ...password]`. The display requires authenticated BLE pairing. |
| Upload key | `692d6042-d8fa-498b-a4db-8a7401d47baf` | Encrypted read: 16 random bytes, hex-encoded by the app for HTTP authorization. |

The display shows a fresh six-digit BLE passkey during pairing. It stores Wi-Fi credentials and the upload key in NVS. The app stores the upload key in the device keychain/keystore. After provisioning, the app reads the current IP from Info on every upload; it does not rely on an old DHCP address.

## Local HTTP

The display listens on port 8080 on its Wi-Fi station address. The app and display must be on the same network. Every endpoint requires `Authorization: Bearer <32 lowercase hex digits>`.

| Request | Result |
| --- | --- |
| `GET /v1/status` | JSON: `protocol`, `deviceId`, `wifiConnected`, `generation`, `state` (`idle`, `applying`, `playing`, or `error`) |
| `POST /v1/animation` | Raw `image/gif` body. Maximum 4 MiB; GIF89a, dimensions 1–480 in each axis. Streams to `/media/test.gif.part` in the internal flash media partition, validates it, and schedules an atomic replacement of `/media/test.gif`. Returns `202` with a generation. |

The app polls status until the returned generation is playing, then shows success. A failed or interrupted transfer retains the previously playing GIF. The board does not require a microSD card for upload or playback.

The plain HTTP transport is confined to the user's Wi-Fi network. BLE pairing protects Wi-Fi credentials and the upload key during provisioning; TLS or application-layer content encryption should be added before treating shared or untrusted LANs as supported environments.
