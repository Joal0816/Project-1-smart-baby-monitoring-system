# Evaluation Plan and Evidence

Record actual measured results; do not substitute design expectations for test evidence. See `docs/ml-evaluation.md` for the held-out classifier report.

| Area | Minimum evidence |
|---|---|
| Audio classifier | At least 20 unseen cry and 20 unseen background examples, with speech/music/household-noise/silence confusers; source, license, balance, counts, conditions, confusion matrix, precision, recall, F1, false alarms, failures |
| End-to-end alert | At least 10 triggered trials; capture-to-notification latency and successful/failed deliveries |
| Sensors | Timestamped temperature, humidity, light, proximity, and device-motion/tilt under controlled changes; reference comparison and limitations |
| Real-time firmware | Concurrent task periods, scheduling, heap use, stack high-water marks, worst observed latency, dropped-buffer counts |
| Connectivity | Wi-Fi disconnection/recovery, device fault state, local alerts, queue behavior, and post-recovery event handling |
| Android UI | History/search, stale-data age, acknowledgement, reset/restart, API errors, normal/alert/fault states, large text, dark theme, reduced motion |
| Software quality | Unit tests for event and data handling, static-analysis findings, API authentication/input tests, logs |

## Sensor bring-up evidence (2026-11-13)

| Sensor | Observed on hardware | Conditions / limitations |
|---|---|---|
| AHT21 temperature/humidity | 30.8–30.9 °C, 78.5–79.1 % RH over repeated 1 Hz samples | Indoor desk conditions; no reference thermometer/hygrometer yet. The AHT21 NACKs the AHT10 normal-mode command but reports valid calibration and measurement bits. |
| AP3216C ambient light | 25.6–27.7 lux | Indoor lighting; no controlled light change or lux meter reference yet. |
| AP3216C proximity | 0–8 counts | No deliberate obstruction; proximity is only a local obstruction/placement indication, never infant presence. |
| ICM20608 accelerometer | raw ≈ 120–230 / 1260–1360 / 17300–17540 | Board at rest on a desk; no controlled tilt test yet. Z-axis is near 1 g. |
| ICM20608 gyroscope | near zero (typically ±60 counts) | Board at rest; no controlled rotation test yet. |

The I²C scan also found the ES8388 codec at `0x10` on the AP3216C/ICM20608 bus.

## Audio capture evidence (2026-11-13)

| Item | Result |
|---|---|
| Codec | ES8388 at `0x10` on soft I²C2; configured from the Spark-1 BSP sequence; codec is I2S slave, STM32 is I2S3 master. |
| I2S/DMA | Main I2S3 master TX (silence) + I2S3ext slave RX on PB4; RX DMA1 Stream0 Channel3, TX DMA1 Stream5 Channel0. The reference-manual I2S3ext RX DMA row is wrong; Stream0/Channel3 works. |
| Format | 16 kHz stereo, signed PCM16, 1024-sample circular DMA buffer (512-sample halves). |
| Timing | Measured `half_us = 15.7 ms` for 512 samples, i.e. ≈32.6k samples/s or ≈16.3k stereo frames/s, consistent with the 16 kHz configuration. |
| Signal | A 3.5 mm microphone is connected to CN3. Ambient RMS ≈ 45–168; live events reached `max_rms=1565`, `max_peak=8130`. |
| Two-second window | Mono downmix buffer of 32,000 samples (64 KiB). A live microphone event triggered the gate and filled the window; RAM dump measured RMS 113.32, peak 1108, mean -4.39, 6,999 zero crossings. A local WAV was saved outside git for inspection. No classifier has run on it. |

The next audio step is to upload or stream this bounded window to the classifier; live microphone capture, the activity gate, and the 2 s buffer are verified.

## Classifier evaluation protocol (not yet executed)

- **Model**: pretrained YAMNet via TensorFlow Hub, class index for `baby cry, infant cry`; cited as a public AudioSet model. Backend inference is chosen because the YAMNet/TensorFlow runtime does not fit the STM32F407 flash/RAM budget; the firmware only uploads bounded 2 s mono PCM16 windows.
- **Candidate data**: ESC-50 `crying_baby` clips (40 × 5 s, CC BY-NC 3.0) for cry examples and non-cry ESC-50 classes for background; record the dataset URL, licence, class balance, and exact file list. Any speech/music/household-noise confusers must come from a separately licensed source and be documented.
- **Split**: hold out 20 cry and 20 non-cry clips that are never used to choose the threshold. Use a separate validation subset for threshold selection; no clip may appear in both subsets.
- **Preprocessing**: decode to 16 kHz mono, signed PCM16, take a 2 s window, and document trimming/padding and normalisation. Do not augment the test set.
- **Metrics**: confusion matrix, precision, recall, F1, false-positive count, missed-cry count, and the per-clip score distribution. Report failure examples (speech, music, silence, household noise).
- **Decision rule**: default score threshold 0.35 with two adjacent YAMNet frames above threshold; select the threshold on validation data with recall prioritised, then evaluate once on the held-out set.
- **Latency**: measure on-device capture-to-upload, backend inference, persistence, and notification separately; report p50/p95 over at least 10 triggered trials.
- **Runtime note**: TensorFlow CPU 2.21.0 + TensorFlow Hub 0.16.1 + setuptools `<81` works after `Unblock-File` on Windows. The default class-map URL was 404 and was replaced with the TensorFlow models GitHub CSV.

## YAMNet smoke test (not the required 20/20 evaluation)

| Clip | 2 s window | Max cry score | Confirmed (0.35, 2 frames) |
|---|---|---|---|
| Live microphone, `audio_window.bin` | captured | 0.0025 | No (background) |
| ESC-50 `1-187207-A-20.wav` | beginning | 0.1297 | No |
| ESC-50 `1-187207-A-20.wav` | middle | 0.1890 | No |
| ESC-50 `1-211527-A-20.wav` | beginning | 0.6493 | No (streak 1) |
| ESC-50 `1-211527-A-20.wav` | middle | 0.9295 | Yes (streak 4) |
| ESC-50 `1-211527-B-20.wav` | beginning | 0.8746 | No (streak 1) |
| ESC-50 `1-211527-B-20.wav` | middle | 0.2966 | No |

These are three ESC-50 crying-baby clips (CC BY-NC 3.0), downloaded outside git. They are **not** the held-out 20 cry + 20 non-cry set; they only show that the adapter runs and that the two-frame rule changes outcomes. Several high-scoring windows were rejected because only one YAMNet frame exceeded 0.35, which needs review during threshold/confirmation calibration.

## Android runtime evidence (2026-11-13)

- APK installed on an API 35 x86_64 emulator (`LittleMonitor`) with WHPX acceleration.
- Debug configuration: `http://10.0.2.2:8000`, app token, device `rt-spark-01`; the backend and serial telemetry bridge were running on the host.
- Dashboard displayed live readings (about 30.3 °C, 84 % RH), with the cry-inference-unavailable fault state visible.
- History displayed a `Likely baby cry detected` event at 92 % confidence plus temperature/humidity range events; `Mark reviewed` changed the cry event to `Reviewed`.
- Search filtered History to the cry event.
- Dark theme rendered correctly; 1.5× font scale was tested and a header/status-pill overlap was fixed by stacking the pill below the title.
- A fresh direct API cry window (`esc50-cry-window.bin`, 0.92954, event `b80c5279-4985-4493-8593-263ca0636513`) produced the app’s `CRY ALERT · REVIEW` card and a local notification (`Likely baby cry detected · model confidence 93%`). This confirms the backend-to-app alert UI and local fallback; it is still API-to-app, not microphone-to-app.
- Ten sequential notification trials delivered **10/10** notifications. API-event-to-local-notification latency: min 5.38 s, median 14.18 s, p95 14.6 s, max 14.93 s. The 15 s app poll interval dominates; this is **not** capture-to-notification latency and not an FCM push trial.
- Landscape rotation rendered correctly; API-error state showed `Update failed` while retaining the last readings.
- After stopping the telemetry bridge for more than 120 s, the app showed `Offline`, `Data stale`, `Last update · 2 min ago`, and `Audio result is out of date`. The `safeDrawing` inset fix keeps content below the system status bar.
- Reduced-motion check: system animation scales set to 0; the app still rendered and remained usable.
- The serial bridge was deliberately disrupted by the ST-Link VCP disappearing; it auto-detected the reappearing COM7 port and resumed posting without a restart.
- FCM notification delivery: ten backend events produced ten delivered notifications (`push_sent=1` and ten distinct `dumpsys` titles) with locally configured Firebase credentials. See `docs/fcm-setup.md`.

## Backend audio path test (real YAMNet)

Using FastAPI's test client with the real ML classifier and a temporary SQLite database:

| Upload | Response | Event |
|---|---|---|
| Live mic window, 64,000 bytes | 200, `background`, confidence 0.00248 | None |
| ESC-50 cry window, 64,000 bytes | 200, `baby_crying`, confidence 0.92954, confirmed | Persisted `cry` event, `push_sent=0` |

This verifies the audio endpoint, classifier adapter, persistence, and event creation. It is not the 20/20 held-out evaluation. FCM is now configured locally and separately verified with ten delivered notifications.

## Board TLS and audio transport evidence (2026-11-13)

| Check | Result |
|---|---|
| TLS terminator | Caddy 2.11.9 on the PC, certificate for `baby-monitor.local`, reverse proxy to FastAPI on `127.0.0.1:8000`. |
| Cipher | `openssl s_client` and the board negotiated TLS 1.2 `ECDHE-RSA-AES128-GCM-SHA256`; certificate verification returned 0 (ok). |
| Board request | RW007 + mbedTLS + pinned CA sent telemetry and received `HTTP/1.1 200 OK`; no `SECLEVEL=0` override is used. |
| Audio POST | With the activity gate temporarily lowered to RMS 100 for transport testing, the board captured and uploaded three consecutive 64,000-byte windows (sequences 0–2); each received `HTTP/1.1 200 OK`, and the backend recorded the latest status as `background` (no cry event). The production gate was restored to RMS 800 afterward. |
| Failure behavior | On a 30 s AP outage the board retained a bounded 8-slot telemetry queue; after reconnecting it drained the queue (`q=6`, `dropped=6`) and resumed authenticated 200 responses. |

This proves the end-to-end transport, authentication, audio framing, backend classifier call, and queue recovery. Clearing the ES8388 `ADCCONTROL3` ASDOUT tri-state and using I2S 16-bit format also produced real onboard-microphone windows. Ten current-firmware microphone-to-App trials are recorded in `docs/test-evidence.md`: 10/10 local notifications delivered, capture-to-notification min 1.166 s, median 6.2705 s, mean 7.211 s, p95/max 15.815 s. The notifications in that ten-trial set use the app's 15 s polling fallback; FCM delivery was verified separately with ten backend events (`docs/fcm-setup.md`).

## Safe test method

Use a classroom fixture and prerecorded audio with recorded source/license details. Do not attach electronics/cables to an infant or place them in a sleep space. Do not describe the prototype as a medical or safety device.

## Results template

| Metric | Result | Conditions / notes |
|---|---|---|
| Cry precision / recall / F1 | Test @0.05: 1.000 / 0.850 / 0.919; test @0.35: 1.000 / 0.800 / 0.889 | 20 unseen cry + 20 unseen non-cry ESC-50 clips, source-disjoint. See `docs/ml-evaluation.md`. |
| False alerts / missed cries | 0 false positives; 3 FN @0.05, 4 FN @0.35 | Misses: `1-211527-B-20`, `2-107351-A-20`, `2-66637-A-20`; `1-187207-A-20` also missed at 0.35. |
| Capture-to-notification latency | 10/10 real microphone-to-App local notifications delivered; min 1.166 s, median 6.2705 s, mean 7.211 s, p95/max 15.815 s | Onboard electret; licensed cry source held at mic; app 15 s polling fallback for this set; separate FCM delivery verified. See `docs/test-evidence.md`. |
| FreeRTOS heap / minimum stack | 9,448 B minimum free of 24,576 B; stack high-water 96/236/142/233/125 words (Sensors/Audio/Network/LocalUI/Health) | Five tasks; latest build 73,364 B main SRAM + 64 KiB CCM audio window, 139,356 B flash; 168 MHz HSE/PLL. Worst-case latency and longer-run soak pending. |
| Worst observed task latency | TBD | Include measurement method |
| Wi-Fi recovery and queued events | 30 s AP outage: board retained a bounded queue and resumed 200 responses after reconnect; `q=6`, `dropped=6` | Single trial; no app screenshot captured during this run; audio-event queueing still requires controlled cry input |
| Android runtime | Dashboard, History, acknowledgement and fault state verified on API 35 emulator | Search, stale transition, API error, large text, dark theme, reduced motion, restart/rotation, and FCM delivery verified (10/10). |
