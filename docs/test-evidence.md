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
RAM:   56.0% (73,364 / 131,072 bytes)
Flash: 13.3% (139,356 / 1,048,576 bytes)

PLATFORMIO_CORE_DIR=.pio-core pio run -e rtspark_f407zg_hsi -t upload
SUCCESS
```

Runtime health observed during the final smoke test: `heap_min=9448`, `stacks=96/236/142/233/125`, telemetry `HTTP/1.1 200 OK`, queue `q=1..2`, `dropped=0`.

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

The root cause was the ES8388 ADC output tri-state: `ADCCONTROL3=0x02` set `TRI=1`, leaving ASDOUT floating. Clearing it to `0x00` and using I2S 16-bit format (`ADCCONTROL4=0x0C`) produced real onboard-microphone windows. Two close cry windows were classified `baby_crying`: event `b9075663-d35d-43e3-846c-ec6440652fed` at 0.91179 (91% notification) and event `f6d0f8ad-b729-4a70-9016-8a2d54e5b5cb` at 0.95311 (95% notification). These are two real capture-to-notification trials; the remaining eight are still required. The source ESC-50 cry window measures RMS 3,624, peak 23,483, and 8,939 zero crossings. The mismatch is in the microphone/acoustic input chain, not the TLS upload or classifier adapter.

## Not yet evidenced

- Controlled acoustic cry input triggering the RMS-800 gate and 10 microphone-to-app trials.
- Real FCM delivery (no service-account/project credentials).
- Display, buttons, buzzer, and local acknowledge/mute.
- Longer FreeRTOS latency/soak testing and a fully tagged submission.
