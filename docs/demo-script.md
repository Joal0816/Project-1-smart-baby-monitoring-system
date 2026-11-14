# Demonstration Video Script and Evidence Checklist

The video must show the real board, the backend, and the Android app together. Use a classroom fixture and licensed prerecorded cry audio. Do not attach electronics or cables to an infant; do not present the prototype as a medical or safety device.

## Pre-record setup

1. Start the backend on `127.0.0.1:8000` with the local database and YAMNet installed.
2. Start Caddy with `tools/Caddyfile.example`; verify the board CA is pinned and port 8443 is listening.
3. Start the PC hotspot and confirm the board obtains `192.168.137.x`.
4. Start the Android emulator/device, open Settings, and enter the HTTPS endpoint plus the app token and `rt-spark-01`. Confirm live readings before recording.
5. Put a 3.5 mm microphone on CN3 or the classroom audio fixture; verify `audio: halves=...` advances. If the RMS gate does not trigger, do not fake the result: show the ambient trace and state the calibration limitation.
6. Stop the serial bridge before using COM7 for a capture; restart it only after the board is idle.

## Shot list

| Shot | Duration | What to show |
|---|---:|---|
| 1. Board and safety statement | 15 s | Board, ST-Link, microphone/fixture, no infant; state that this is an educational prototype, not a safety device. |
| 2. Serial boot | 15 s | `STM32F407`, 168 MHz, sensors, ES8388, RW007 join, DHCP address. |
| 3. Live app | 20 s | Dashboard temperature/humidity/light/proximity, `Online`, inference status. |
| 4. TLS evidence | 15 s | Caddy/`openssl s_client` output showing TLS 1.2 `ECDHE-RSA-AES128-GCM-SHA256` and certificate verification; board serial showing `HTTP/1.1 200 OK`. |
| 5. Cry input | 20 s | Play the licensed cry fixture; show `audio: window ready`, `tls audio headers`, `HTTP/1.1 200 OK`; backend response label. |
| 6. App alert | 20 s | `Possible baby cry` notification and History event with confidence. |
| 7. Acknowledgement | 10 s | Tap `Mark reviewed`; History updates. |
| 8. Wi-Fi loss/recovery | 30 s | Disable the hotspot; app shows offline/stale; re-enable; board reconnects and drains queued telemetry. |
| 9. Limitations | 15 s | State missing display/buttons and that the classifier is not diagnostic; show FCM delivery if credentials are available. |

## Ten-trial record

For each trial, record this row before moving on. Use the backend event ID and the app notification timestamp; do not reuse an earlier trial.

| # | Capture time | Audio trigger | Label / confidence | Event ID | Backend received | App notified | Latency | Delivered? | Notes |
|---:|---|---|---|---|---|---|---:|---|---|
| 1 | | | | | | | | | |
| 2 | | | | | | | | | |
| 3 | | | | | | | | | |
| 4 | | | | | | | | | |
| 5 | | | | | | | | | |
| 6 | | | | | | | | | |
| 7 | | | | | | | | | |
| 8 | | | | | | | | | |
| 9 | | | | | | | | | |
| 10 | | | | | | | | | |

Then report min/median/p95/max and every failed delivery. Keep the raw serial logs and backend rows outside Git if they contain local network details.

## Current evidence

Ten real onboard-microphone capture-to-notification trials are complete: 10/10 local Android notifications, median 6.27 s, max 15.82 s. Reuse the ten-trial table above for the final recording. The FCM path is verified separately; the physical-fixture video remains outstanding.

## Recorded demonstration file

`docs/little-monitor-demo.mp4` is a 66 s narrated screen recording of the Android app against the live local backend. It shows the dashboard, History with the ten real microphone trials, review acknowledgement, a live cry alert, notification shade, and Settings. The recording uses an API/classifier cry event from a licensed ESC-50 window; it does not replace a video of the physical board fixture. FCM setup is documented in `fcm-setup.md`.
