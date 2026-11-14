# Smart Baby Monitoring System — Project Report

## 1. Objective

Build an educational IoT baby-monitor prototype on the RT-Thread RT-Spark STM32F407 board using STM32Cube HAL and FreeRTOS, with environmental sensing, bounded audio capture, backend cry classification, a local FastAPI/SQLite service, and an Android caregiver app. The prototype is not a medical or safety-certified device.

## 2. Architecture

```mermaid
flowchart LR
  S[Sensors + microphone] --> F[STM32F407<br/>HAL + FreeRTOS]
  F -->|16 kHz PCM16 window| B[FastAPI backend]
  B --> Y[YAMNet inference]
  B --> D[(SQLite)]
  B --> A[Android app]
  A -->|acknowledgement| B
```

The onboard RW007 transport joins a 2.4 GHz AP, obtains DHCP, and posts real sensor telemetry over TLS 1.2 using ECDHE-RSA-AES128-GCM-SHA256 with a pinned local CA. A Caddy terminator reverse-proxies HTTPS to FastAPI; the board’s authenticated 64 KB audio-window POST is accepted by the backend. The firmware has a bounded telemetry queue and post-recovery drain; a 30 s AP outage trial reconnected and drained without losing an accepted event. Ten real microphone-to-app trials are complete: 10/10 local notifications, capture-to-notification min 1.166 s, median 6.2705 s, mean 7.211 s, p95/max 15.815 s.

## 3. Component inventory

| Component | Verified use |
|---|---|
| STM32F407ZGTx, 1 MiB flash | 168 MHz from 8 MHz HSE/PLL; SWD flash/debug |
| USART1 PA9/PA10 | 115200 console through the ST-Link VCP |
| PF12 / PF11 LEDs | PF12 red LED 1 Hz task indicator |
| AHT21 temperature/humidity | Soft I²C3, PE0/PE1, address 0x38 |
| AP3216C light/proximity | Soft I²C2, PF0/PF1, address 0x1E |
| ICM20608 accelerometer/gyro | Same I²C2 bus, address 0x68, `WHO_AM_I=0xAF` |
| ES8388 + 3.5 mm microphone | Codec at 0x10; I2S3 master, I2S3ext RX, DMA1 |
| RW007 Wi-Fi | SPI link, Wi-Fi join, DHCP, pinned-CA TLS 1.2 AES-GCM, real sensor telemetry, 64 KB audio-window POST, bounded outage queue, ten microphone-to-app trials, and ten FCM delivery trials verified; emulator and photo-based physical demo videos recorded |
| ST7789 LCD, buttons, buzzer | FSMC ST7789 initialized; three status pages; KEY0–KEY2/WK_UP; PB0 buzzer; red/blue audio meter; display/mute/beep observed |
| Android phone/emulator | API 35 emulator; APK connected to local backend; FCM push verified 10/10 |
| Local UI | ST7789 240×240, buttons, blue alert LED, buzzer |

## 4. Firmware design

Five FreeRTOS tasks are created:

| Task | Priority | Stack (words) | Current behavior |
|---|---:|---:|---|
| Sensors | 4 | 256 | Initializes and reads AHT21/AP3216C/ICM20608 every second |
| Audio | 5 | 384 | ES8388 + I2S3ext DMA, RMS/peak logging, 2 s activity-gated mono window |
| Network | 2 | 2048 | RW007 + lwIP DHCP/TCP; authenticated telemetry POST verified |
| Local UI | 3 | 256 | PF12 blink |
| Health | 1 | 256 | 1 Hz heap/stack/health logging |

Latest measured UI build: 74,108 B of 131,072 B main SRAM (plus the 64 KiB CCM audio window); 145,100 B of 1,048,576 B flash. A 600 s soak collected 595 health samples with 8,424 B minimum free of 24,576 B FreeRTOS heap, stack high-water 96/230/132/342/113 words (Sensors/Audio/Network/LocalUI/Health), 264 HTTP 200 responses, 82 accepted audio windows, 0 audio DMA errors, and 0 reboots. Max wake delay was 0/9/0/4/507 ms; the lowest-priority health task is delayed by USB logging and network activity. Tick is 1 kHz; the sensor bus uses a FreeRTOS mutex shared with the codec.

### Audio

- ES8388 configured from the Spark-1 BSP sequence.
- STM32 I2S3 master TX supplies clocks and silence; I2S3ext slave RX receives codec ADC data on PB4.
- DMA1 Stream0 Channel3 RX (the reference-manual I2S3ext RX row is wrong) and Stream5 Channel0 TX.
- 16 kHz stereo PCM16, 1024-sample circular DMA buffer; measured half-buffer period 15.7 ms for 512 samples.
- Downmixed into a 32,000-sample mono window after the provisional RMS gate (800) triggers; window completion and RAM-dump RMS/peak were verified.
- No continuous audio is streamed or stored by default.

## 5. Classifier and backend

- Pretrained YAMNet (`baby cry, infant cry`), backend inference; TensorFlow CPU 2.21.0 / Hub 0.16.1.
- Decision rule: threshold plus two adjacent YAMNet frames; default threshold 0.35.
- Backend stores readings, events, acknowledgements, and inference status in SQLite; 15 backend tests pass.
- Held-out ESC-50 evaluation: 20 unseen cry + 20 unseen non-cry clips, source-disjoint; test precision 1.000, recall 0.850, F1 0.919 at threshold 0.05; precision 1.000, recall 0.800, F1 0.889 at default 0.35; zero false positives in this set. See `docs/ml-evaluation.md`.
- Limitations: ESC-50 is small and source-dependent; speech, music, and silence confusers were not included; the max-RMS window selection may overstate performance.

## 6. Android app

- Jetpack Compose dashboard, History, Settings, and acknowledgement flow.
- Token encrypted with Android Keystore; release builds require HTTPS. The debug build has a local-only cleartext allowance for `10.0.2.2`.
- Verified on API 35 emulator: live readings, history, search, acknowledgement, dark theme, 1.5× text, landscape, API-error state, stale/offline state, safe drawing insets, and a local `Possible baby cry` notification.
- Ten local notification trials delivered 10/10; API-event-to-app notification latency min 5.38 s, median 14.18 s, p95 14.6 s, max 14.93 s. This is polling latency, not capture-to-notification.
- FCM was configured locally with project `little-monitor`; ten distinct backend events produced ten `push_sent=1` records and ten visible FCM notifications while the app was backgrounded. Setup and evidence: `docs/fcm-setup.md`.

## 7. Safety and privacy

- No electronics or cables are attached to an infant; testing uses a classroom fixture and licensed audio.
- Proximity is a placement/obstruction indication only; device motion is not infant movement; cry classification does not diagnose a condition.
- Missing readings, stale inference, network loss, and exhausted queues are explicit faults, never evidence of safety.
- Only bounded triggered windows are intended for upload; the backend processes audio in memory and does not persist raw audio.
- The local bench bridge and debug HTTP path are development aids, not a deployment design.

## 8. Remaining or optional work

1. Speech/music/silence confusers and a larger independent test set.
2. Longer FreeRTOS heap/stack/timing soak and worst-case latency measurement.
3. Live-motion physical-fixture footage. Release tag `v1.0.0-bca182` is created; the repository includes an emulator recording and a photo-based physical cut (real board photographs plus the app recording).

## 9. Conclusion

The prototype now demonstrates the complete local service path: STM32Cube/FreeRTOS firmware reads the onboard sensors and microphone, RW007 joins Wi-Fi and obtains DHCP, mbedTLS verifies a pinned CA and negotiates TLS 1.2 AES-GCM, Caddy terminates HTTPS, FastAPI stores readings/events and runs YAMNet, and the Android app shows live state, history, acknowledgement, and local cry notifications. The classifier has a reproducible held-out evaluation, and the firmware upload, outage queue, and Android build have direct evidence.

The capture-to-notification path is verified with ten real onboard-microphone trials and ten local Android notifications, and FCM delivery is verified with ten additional backend events. The Android app, classifier evaluation, TLS transport, queue recovery, and documentation are in the repository. The local display, buttons, buzzer, and audio-reactive LEDs are implemented; longer soak testing and live-motion footage remain optional. The prototype is educational and is not a medical or safety-certified monitor.
