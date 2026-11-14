# Demonstration Video Script and Evidence Checklist

The video must show the real board, the backend, and the Android app together. Use a classroom fixture and licensed prerecorded cry audio. Do not attach electronics or cables to an infant; do not present the prototype as a medical or safety device.

## Pre-record setup

1. Start the backend on `127.0.0.1:8000` with the local database and YAMNet installed.
2. Start Caddy with `tools/Caddyfile.example`; verify the board CA is pinned and port 8443 is listening.
3. Start the PC hotspot and confirm the board obtains `192.168.137.x`.
4. Use a **physical Android phone** on the same hotspot for the final live demo (the emulator is only a fallback). Open the app, enter the HTTPS endpoint plus the app token and `rt-spark-01`, and confirm live readings before recording. Keep the phone visible in the same frame as the board for the cry/notification shot.
5. Put a 3.5 mm microphone on CN3 or the classroom audio fixture; verify `audio: halves=...` advances. If the RMS gate does not trigger, do not fake the result: show the ambient trace and state the calibration limitation.
6. Stop the serial bridge before using COM7 for a capture; restart it only after the board is idle.

## Shot list

| Shot | Duration | What to show |
|---|---:|---|
| 1. Board and safety statement | 15 s | Real RT-Spark board, ST-Link, microphone/fixture, Android phone, no infant; state that this is an educational prototype, not a safety device. |
| 2. Matrix and LCD | 20 s | Plastic 19-LED matrix: white boot flash, dim blue idle, red/yellow/blue with low/mid/high sound; LCD `SOUND COLOUR` page. |
| 3. Serial boot | 15 s | `STM32F407`, 168 MHz, sensors, ES8388, RW007 join, DHCP address. |
| 4. Live app | 20 s | Dashboard temperature/humidity/light/proximity, `Online`, inference status. |
| 5. TLS evidence | 15 s | Caddy/`openssl s_client` output showing TLS 1.2 `ECDHE-RSA-AES128-GCM-SHA256` and certificate verification; board serial showing `HTTP/1.1 200 OK`. |
| 6. Cry input | 20 s | Play the licensed cry fixture; show `audio: window ready`, `tls audio headers`, `HTTP/1.1 200 OK`; backend response label. |
| 7. App alert | 20 s | `Possible baby cry` notification and History event with confidence. |
| 8. Acknowledgement | 10 s | Tap `Mark reviewed`; History updates. |
| 9. Wi-Fi loss/recovery | 30 s | Disable the hotspot; app shows offline/stale; re-enable; board reconnects and drains queued telemetry. |
| 10. Limitations | 15 s | State that the fixture is not a medical device, the classifier is not diagnostic, and that longer live-motion coverage remains optional; show FCM delivery if credentials are available. |

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

Ten real onboard-microphone capture-to-notification trials are complete: 10/10 local Android notifications, median 6.27 s, max 15.82 s. Reuse the ten-trial table above for the final recording. The FCM path is verified separately. The final video must use live board motion and a live phone, not the earlier photo-based physical cut; the matrix/plastic panel must be visibly driven by sound.

## Recorded demonstration files

- `docs/little-monitor-demo.mp4`: 66 s narrated emulator screen recording. It shows the dashboard, History with the ten real microphone trials, review acknowledgement, a live cry alert, notification shade, and Settings.
- `docs/little-monitor-physical-demo.mp4`: 66 s narrated cut using the supplied real RT-Spark board photographs (two angles) followed by the app recording. The board section is a photo sequence, not live board motion; replace it for the final submission with the live board + phone recording. FCM setup is documented in `fcm-setup.md`.
