# Test Evidence Index

All results below were run on the project workstation on 2026-11-13 (Asia/Singapore). Raw capture logs and local certificates are kept outside Git under `.dev-tools/`; they contain local addresses and are not submission artifacts.

## Environment

| Item | Value |
|---|---|
| Board | RT-Spark STM32F407ZGT6, SWD via ST-Link VCP on COM7 |
| Firmware | PlatformIO, `framework = stm32cube`, FreeRTOS, HSE/PLL 168 MHz |
| Backend | FastAPI + Uvicorn on `127.0.0.1:8000`, real YAMNet classifier |
| TLS terminator | Caddy 2.11.9 on `:8443`, certificate for `baby-monitor.local`, pinned local CA |
| Android | API 35 x86_64 emulator, debug APK |
| Backend tests | 15 passed |

## Firmware build and flash

```text
PLATFORMIO_CORE_DIR=.pio-core pio run -e rtspark_f407zg_hsi
RAM:   56.5% (74,100 / 131,072 bytes)
Flash: 13.3% (139,356 / 1,048,576 bytes)

PLATFORMIO_CORE_DIR=.pio-core pio run -e rtspark_f407zg_hsi -t upload
SUCCESS
```

Runtime health observed after the local-UI build: `heap_min=8424`, `stacks=92/234/142/354/113`, display `ST7789 240x240 ready`, telemetry `HTTP/1.1 200 OK`, queue drain active.

## Local hardware UI

The LocalUI task now initializes the onboard ST7789 (FSMC bank 3), three 240×240 status pages, KEY0–KEY2/WK_UP, the blue alert LED, and the PB0 buzzer. Serial evidence after flashing: `display: ST7789 240x240 ready` and `ui: display=ready; buttons and buzzer active`. Heap minimum is 8,424 B; stack high-water is 92/234/142/354/113 words. Human check: display rendered the status and `BUZZER MUTED` states; the WK_UP test beep was audible. The AudioTask logs `freq=...` and the red/blue LED meter responds to level/frequency; a 1 kHz laptop tone produced a coarse 625–1562 Hz estimate. Pin map and behavior: `docs/local-ui.md`.

## FreeRTOS wake-delay measurement (2026-11-14)

The firmware now records, per periodic task, the delay from its intended wake tick to its first instruction. A 120 s capture after reflashing contained 120 health lines (124 sensor heartbeats):

| Task | Max wake delay |
|---|---:|
| Sensors (1 s period) | 0 ms |
| Audio (5 ms period) | 3 ms |
| Network (event-driven) | not applicable; logged as 0 |
| LocalUI (500 ms period) | 0 ms |
| Health (1 s period, lowest priority, USB logging) | 482 ms |

Heap minimum 9,448 B of 24,576 B; stack high-water 96/234/142/229/113 words. The health delay is dominated by lowest-priority USB logging and network/TLS activity; it is reported as an observed scheduler delay, not a hard real-time guarantee. Raw capture: `.dev-tools/downloads/timing-120s.log` (ignored generated evidence). Build: 73,388 B RAM, 139,612 B flash, 168 MHz.

## TLS

```text
openssl s_client -connect 127.0.0.1:8443 -servername baby-monitor.local \
  -CAfile .dev-tools/tls/ca.crt -tls1_2 -cipher ECDHE-RSA-AES128-GCM-SHA256
Protocol: TLSv1.2
Cipher is ECDHE-RSA-AES128-GCM-SHA256
Verify return code: 0 (ok)
```

Board serial log: `wifi: tls telemetry sent`, then `wifi: http HTTP/1.1 200 OK`. No `SECLEVEL=0` or `VERIFY_OPTIONAL` was used. The clean rebuild was required because mbedTLS cipher-suite objects had been stale; after `pio run -t clean`, the ClientHello changed from CBC `c0 14` to GCM `c0 2f`.

## Audio window transport

With the gate temporarily lowered to RMS 100 (transport test only), the board captured three consecutive 64,000-byte windows and received 200 responses:

```text
audio: window ready samples=32000 rms=49 peak=1260
wifi: tls audio headers sent seq=0 bytes=64000
wifi: http HTTP/1.1 200 OK
wifi: audio accepted seq=0
```

Backend `audio_status` recorded `background`; no cry event was created. The production gate was restored to RMS 800. A real cry window posted directly through the backend produced `baby_crying` at 0.92954 and a persisted cry event; that is classifier/API evidence, not microphone capture evidence.

## Wi-Fi outage and queue

A 70 s serial capture included a 30 s AP stop/start:

```text
state=Off
state=On

wifi: ip=192.168.137.68 ... q=6 dropped=6
wifi: http HTTP/1.1 200 OK
wifi: http closed status=200 rx=309
```

The board retained a bounded 8-slot telemetry queue, reconnected, and resumed authenticated 200 responses. Queue production is rate-limited to one sample per 5 s; when the backlog exceeds four slots, the drain interval shortens to 1 s.

## Android

```text
cd android
gradlew.bat assembleDebug
BUILD SUCCESSFUL in 5m 34s
```

The APK built against local JDK 17 and Android SDK. The app was previously exercised on the API 35 emulator for live readings, History/search, acknowledgement, dark theme, 1.5× text, landscape, API-error, stale/offline, reduced motion, and local notification. Ten API-event-to-app notification trials delivered 10/10 (median 14.18 s); these are not capture-to-notification trials. FCM is not configured.

## Audio diagnostic

The root cause was the ES8388 ADC output tri-state: `ADCCONTROL3=0x02` set `TRI=1`, leaving ASDOUT floating. Clearing it to `0x00` and using I2S 16-bit format (`ADCCONTROL4=0x0C`) produced real onboard-microphone windows. Ten current-firmware capture-to-notification trials are recorded below; earlier events `b9075663-d35d-43e3-846c-ec6440652fed` (0.91179) and `f6d0f8ad-b729-4a70-9016-8a2d54e5b5cb` (0.95311) were separate exploratory captures.

## Ten real microphone-to-app trials (2026-11-14)

The board used the verified onboard electret (LINPUT1), `ADCCONTROL3=0x00` (ASDOUT enabled), `ADCCONTROL4=0x0C` (I2S 16-bit), gain `0xBB`, and RMS gate 800. A loud licensed cry source was held at the microphone; each cry episode was separated with three background tone windows so the backend created a new event. Notification times are the Android notification `when` values; latency is `notification - captured_at`.

| # | Event ID | Confidence | Captured (UTC) | Notification (UTC) | Latency |
|---:|---|---:|---|---|---:|
| 1 | `412c9115-a887-40b0-a874-fbbfecffa223` | 0.98010 | 04:08:48 | 04:08:52.513 | 4.513 s |
| 2 | `8cac9e91-17d5-4ded-9ec7-94f7b0ba24f1` | 0.86339 | 04:10:39 | 04:10:42.079 | 3.079 s |
| 3 | `d9c02897-59c8-488a-8250-f543899594bc` | 0.75471 | 04:12:44 | 04:12:47.296 | 3.296 s |
| 4 | `f88dce2e-73c4-4214-ac98-907b62deb5fc` | 0.82309 | 04:13:28 | 04:13:34.294 | 6.294 s |
| 5 | `de70c971-e519-48b0-89f9-ba17af25b991` | 0.75370 | 04:14:14 | 04:14:21.275 | 7.275 s |
| 6 | `77c3bb4f-f008-4463-9dcf-d563ef764be8` | 0.77082 | 04:15:02 | 04:15:08.247 | 6.247 s |
| 7 | `5c8fadfc-d7c2-4385-87fb-7be110fefc08` | 0.56205 | 04:16:00 | 04:16:10.866 | 10.866 s |
| 8 | `750ee84d-3a19-4ce7-b63e-07336618dc08` | 0.81724 | 04:16:41 | 04:16:42.166 | 1.166 s |
| 9 | `d7a28af0-01e5-43e3-b315-965e2ec9c598` | 0.83642 | 04:17:47 | 04:18:00.563 | 13.563 s |
| 10 | `7582900a-1369-43a7-9056-629c9ad9f72b` | 0.66058 | 04:19:03 | 04:19:18.815 | 15.815 s |

**Delivery: 10/10 local Android notifications.** Latency min 1.166 s, median 6.2705 s, mean 7.211 s, p95/max 15.815 s. Trial 8’s notification timestamp preceded the backend `created_at` by 1.2 s because the emulator/host clocks are not synchronized; the capture-to-notification value still uses the captured and notification timestamps. These are local polling notifications; FCM is verified separately below.

## FCM delivery (2026-11-14)

With the local Firebase project (`little-monitor`), `google-services.json`, and a service-account key configured outside Git, the backend sent ten distinct test events. `push_sent=1` for all ten, and ten distinct `FCM delivery trial N` titles were present in `adb shell dumpsys notification`. Two API/classifier cry events also produced visible `Likely baby cry detected` FCM notifications. Full setup, event IDs, commands, and limitations: `docs/fcm-setup.md`.

## Not yet evidenced

- Display, buttons, buzzer, and local acknowledge/mute.
- Longer FreeRTOS latency/soak testing and worst-case task latency.
