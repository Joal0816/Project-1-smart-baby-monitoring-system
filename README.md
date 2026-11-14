# Smart Baby Monitoring System

Educational BCA182 Project 1 prototype for an RT-Spark / STM32F407 device. It includes STM32Cube HAL + FreeRTOS firmware, a FastAPI backend, an audio-classification service, an Android caregiver app, and a browser dashboard. The course assignment PDF is not included in this public repository.

> **Safety:** This classroom prototype is not a medical or safety-certified monitor and cannot replace adult supervision. Test only with a classroom fixture and licensed prerecorded audio. Never attach electronics or cables to an infant or place them in an infant sleep space.

## Current status

- Run the local preview from VS Code with **Terminal → Run Task → Start caregiver UI preview (VS Code)**, then open **Browser: Open Integrated Browser** and enter `http://localhost:4174`. The page reloads when preview files change.
- The browser dashboard starts with clearly labeled sample data and can connect to the authenticated FastAPI readings, trends, event history, and event acknowledgement endpoints. Its app bearer token stays in page memory and is cleared on reload; only the backend URL and device ID are saved in this browser.
- Use direct browser-to-API authentication only for local or trusted development. A shared/public deployment needs a server-side authenticated proxy; serve non-local APIs over HTTPS and keep the board’s device token on the device.
- The Android dashboard, History and Settings screens are implemented in Jetpack Compose. It polls the authenticated API every 15 seconds, encrypts the app token with Android Keystore, supports event acknowledgement and FCM registration, and posts a local `Possible baby cry` notification for newly observed unacknowledged cry events. The debug app was exercised on an API 35 emulator against the local backend; FCM remote push was verified with ten delivered notifications.
- Firmware builds and flashes through PlatformIO, runs at 168 MHz from the 8 MHz HSE/PLL, logs over USART1 at 115200 on the ST-Link VCP, blinks the PF12 red LED from a FreeRTOS task, and reads the onboard AHT21 temperature/humidity, AP3216C light/proximity, and ICM20608 accelerometer/gyroscope sensors once per second. It also captures 16 kHz stereo PCM16 from the ES8388 over I2S3/I2S3ext + DMA, with a 2 s activity-gated window. The onboard RW007 joins a 2.4 GHz AP, obtains DHCP, and sends real sensor telemetry every ~5 s over TLS 1.2 using ECDHE-RSA-AES128-GCM-SHA256 with a pinned local CA. A Caddy TLS terminator reverse-proxies to FastAPI; the board’s 64 KB audio-window POST is authenticated and accepted by the backend. The telemetry path has a bounded 8-slot queue with post-recovery drain; a 30 s AP outage trial reconnected and drained without losing an accepted event. The ES8388 ASDOUT tri-state bug was fixed; real onboard-microphone capture produced ten accepted cry events and ten local Android notifications. Capture-to-notification latency was min 1.166 s, median 6.2705 s, mean 7.211 s, p95/max 15.815 s. Display and local controls remain inactive.
- An RT-Spark STM32F407 board is available and has been flashed over SWD. The console, red LED, HSE/PLL clock, three onboard I²C sensors, I2S/DMA audio path, 2 s activity-gated window, RW007 SPI/command transport, Wi-Fi join, DHCP, pinned-CA TLS 1.2 AES-GCM telemetry, authenticated audio-window upload, and bounded outage queue are verified in local trials. Ten real capture-to-notification trials, ten FCM delivery trials, and an emulator demonstration video are complete; display, local controls, and a physical-fixture video remain outstanding.
- No Firebase project or service-account credentials are included. FCM requires the user to configure them locally; the local setup and verified delivery are documented in `docs/fcm-setup.md`.

## Live web preview

The static caregiver dashboard is hosted at [monitor.joalvergs.tech](https://monitor.joalvergs.tech). It opens in sample mode. The FastAPI backend is not hosted by this deployment, so live device readings require a separately configured backend.

## Repository layout

- `web/` — VS Code browser preview for the caregiver dashboard, history, settings, and motion states.
- `android/` — Native Kotlin Android application with Jetpack Compose.
- `backend/` — FastAPI API, SQLite persistence, event rules, and optional YAMNet inference.
- `firmware/` — STM32Cube HAL + FreeRTOS project sources and candidate board manifest.
- `docs/` — board inventory, data flow, safety/privacy, and evaluation evidence.

## Preview in VS Code

In VS Code, run **Terminal → Run Task → Start caregiver UI preview (VS Code)**. Then open **Browser: Open Integrated Browser** and enter `http://localhost:4174`.

The **Settings → Monitor connection** panel connects to the FastAPI service. The default API URL is `http://127.0.0.1:8000`; enter the configured `MONITOR_APP_TOKEN` and a device ID. The API must allow the preview origin (the default backend CORS configuration includes `http://localhost:4174` and `http://127.0.0.1:4174`). The app token is kept in page memory only; reload clears it. Click **Use sample preview** to return to the sample dashboard.

Loading, authentication/network errors, stale readings, and empty readings/events are shown explicitly. When connected, trend lines use stored readings and event acknowledgement is sent to the API. The **Simulate cry alert** control is available only in sample preview; it creates a local demo history row and sends no audio.

## Backend setup

Use Python 3.11–3.13 for the optional TensorFlow/YAMNet dependency set. The API can be installed without the ML extras; audio inference then returns an explicit unavailable state.

```bash
cd backend
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt
cp .env.example .env
```

Set unique values for `MONITOR_APP_TOKEN` and `MONITOR_DEVICE_TOKENS` in the local `.env`. Source it in the shell before starting the API:

```bash
set -a
. ./.env
set +a
MONITOR_HOST=0.0.0.0 python run.py
```

For YAMNet inference, install `requirements-ml.txt`; first model use downloads the model and its class map. The tested set is Python 3.13, TensorFlow CPU 2.21.0, TensorFlow Hub 0.16.1, and setuptools `<81` (Hub still imports `pkg_resources`). The server stores sensor data and event metadata in SQLite. Audio windows are processed in memory and are not written to logs or the database.

On Windows, if TensorFlow import fails with an Application Control or blocked-DLL error, clear the zone marker on the local environment and retry:

```powershell
Get-ChildItem .venv -Recurse -File | Unblock-File
```

For device and phone access over the LAN, serve the API with a locally trusted HTTPS certificate. Never use cleartext transport for audio. Configure Firebase service credentials in environment variables on the server only; never commit them.

## Firmware setup

Open this folder in VS Code with PlatformIO. The manifest targets the STM32F407ZGT6; SWD on the connected board reports DEV_ID `0x413` and 1024 KiB flash. Confirm the board revision and the remaining pin/clock details before enabling peripherals. On Windows, install the signed STSW-LINK009 driver if the ST-Link debug interface reports Code 28; the ST-Link virtual COM port carries the 115200 8N1 logs.

```bash
pio run
pio run -t upload
pio device monitor
```

`framework = stm32cube` is required. FreeRTOS is integrated as middleware; it is not a PlatformIO framework choice. Do not use RT-Thread OS/Studio, Arduino, ESP-IDF, or RT-Thread-only drivers in final firmware.

## Android setup

Open the `android/` folder in Android Studio or build the debug APK from a terminal. The project needs JDK 17 and Android SDK platform 37 (`compileSdk = 37`), because the pinned Compose libraries require API 37:

```bash
cd android
./gradlew assembleDebug
```

The debug APK is written to `android/app/build/outputs/apk/debug/app-debug.apk`. Configure the backend HTTPS URL, app bearer token, and device ID in the app's Settings screen; the token is encrypted with Android Keystore on that phone. Install the locally trusted development CA on the Android test device. To enable push delivery, add your app's `google-services.json` under `android/app/` (it is ignored by Git), set `GOOGLE_APPLICATION_CREDENTIALS` and `FIREBASE_PROJECT_ID` on the backend, and rebuild. The Google services Gradle plugin is applied only when that local JSON file exists. See `docs/fcm-setup.md`.

For a local emulator against the workstation backend, the **debug** build also permits `http://10.0.2.2:8000`; release builds remain HTTPS-only. Use the app token and device ID configured on the backend.

For bench diagnostics, a serial bridge can feed sensor readings into the backend without the onboard Wi-Fi path:

```powershell
$env:MONITOR_DEVICE_TOKEN = "dev-device-token"   # use your local token; do not commit it
.\.venv\Scripts\python.exe tools\serial_telemetry_bridge.py --port auto --device-id rt-spark-01
```

The bridge is a development aid only; final connectivity must use the onboard Wi-Fi with authenticated transport.

## Local checks

The portable alert rules can be tested on the host without an STM32 board:

```bash
./tools/test_firmware_host.sh
```

On Windows without a host `cc`, use the MinGW compiler installed by PlatformIO and add its `bin` directory to `PATH` (the compiler's helper processes need the DLLs there):

```powershell
$env:PLATFORMIO_CORE_DIR = Join-Path (Get-Location) '.pio-core'
$env:PATH = "$env:PLATFORMIO_CORE_DIR\packages\toolchain-gccmingw32\bin;$env:PATH"
gcc -std=c11 -Wall -Wextra -Werror -pedantic -Ifirmware/include firmware/src/alert_logic.c firmware/test/test_alert_logic.c -lm -o test-alert-logic.exe
.\test-alert-logic.exe
```

Backend tests are in `backend/tests/`; install `backend/requirements.txt` and `pytest` before running them. Actual model accuracy, push latency, sensor readings, FreeRTOS timing, and board bring-up remain evidence to collect on configured services and hardware; see `docs/evaluation.md`.

## Evaluation and deliverables

See `docs/evaluation.md` and `docs/ml-evaluation.md` for measured evidence. `docs/demo-script.md` contains the video shot list and the ten-trial record table. The classifier has been evaluated on 20 unseen cry + 20 unseen non-cry ESC-50 clips (test precision 1.000, recall 0.850, F1 0.919 at 0.05; 1.000/0.800/0.889 at the 0.35 default). Ten local notification trials delivered 10/10 (median 14.18 s from API event to app notification). The Wi-Fi outage/recovery queue trial is complete; ten real microphone-to-app trials delivered 10/10 (capture-to-notification median 6.27 s, max 15.82 s). FCM delivery is verified; an emulator demonstration video is included. A physical-fixture video remains outstanding.
