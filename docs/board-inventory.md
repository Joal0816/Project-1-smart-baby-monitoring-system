# Board Inventory and Bring-up Record

An RT-Spark STM32F407 board is available and connected. The official Spark-1 BSP, user manual, and schematic were obtained from `RT-Thread-Studio/sdk-bsp-stm32f407-spark`. The MCU, console, red LED, all three onboard I²C sensors, the I2S3/ES8388 DMA audio path, and live microphone capture have been verified on hardware. The board revision is still unconfirmed; Wi-Fi, display, buttons, buzzer, and storage remain untested.

| Resource | Planned project function | Current status | Evidence to record |
|---|---|---|---|
| STM32F407 MCU | FreeRTOS firmware | Identified over SWD: DEV_ID `0x413`, 1024 KiB flash; BSP lists STM32F407ZGTx, HSE 8 MHz. Firmware runs from HSE/PLL at 168 MHz | OpenOCD identify, official BSP, `core clock: 168000000 Hz (HSE/PLL)`; revision confirmation pending |
| AHT10/AHT21 | Room temperature/humidity | Verified at `0x38` on soft I²C3 (SCL PE0, SDA PE1); live readings 30.8–30.9 °C, 78.5–79.1 % RH. The AHT21 NACKs the AHT10 normal-mode command but measures correctly | I²C scan, repeated readout; reference comparison and controlled humidity test pending |
| AP3216C | Ambient light and obstruction indication | Verified at `0x1E` on soft I²C2 (SCL PF1, SDA PF0); live 25.6–27.7 lux, proximity 0–8 with no deliberate obstruction | I²C scan, repeated readout; controlled light/obstruction test pending |
| ICM20608 | Movement/orientation of monitor device | Verified at `0x68` on soft I²C2; `WHO_AM_I = 0xAF` (BSP does not gate on ID). Live accel ≈ 120–230 / 1260–1360 / 17300–17540, gyro near zero at rest | I²C scan, static readout; controlled tilt test pending. Never infer infant movement |
| Wi-Fi module | Secure API transport | RW007 on SPI2: join, DHCP, TLS 1.2 AES-GCM with pinned CA, telemetry and 64 KB audio POST verified; 30 s AP outage queue/drain verified | Long soak and sustained-throughput measurement pending |
| Display, LEDs, buttons, buzzer | Local readings, status, acknowledge/mute/test | Red LED PF12 visually confirmed blinking at 1 Hz; blue LED PF11 documented and active-low; ST7789 240×240 via FSMC; KEY0 on PC0; buzzer mapping not yet extracted | Pin table and individual HAL test |
| RTC, flash, TF storage | Time and outage metadata queue | Present per BSP; untested | Capacity, driver, persistence/recovery test |
| ES8388 audio codec | Audio capture front end; external microphone input | Configured at `0x10` on soft I²C2; I2S3 master + I2S3ext RX + DMA streams 16 kHz stereo PCM16. Half-buffer period 15.7 ms. Fixing `ADCCONTROL3` (clear ASDOUT tri-state) and using I2S 16-bit format produced live audio windows; ten real cry windows across ten trials classified `baby_crying` and notified the app (10/10, median 6.27 s) | Ten-trial capture-to-notification set complete; longer soak and gain calibration pending |
| External microphone | Required audio capture | CN3 is a PJ-320A four-conductor headset jack. The onboard M1 path on LINPUT1 is active after clearing the ES8388 ASDOUT tri-state; leave CN3 empty for the onboard mic. | Use the onboard mic for the remaining ten trials; external TRS/TRRS compatibility is not required |

## Pin map (verified or documented)

| Signal | Peripheral | MCU pin | Board connector | Verified |
|---|---|---|---|---|
| UART log | USART1 | PA9 (TX), PA10 (RX) | ST-Link VCP, COM7 | Yes (115200 8N1) |
| Red LED | GPIO | PF12 | Onboard RGB LED | Yes (user observed 1 Hz blink) |
| Blue LED | GPIO | PF11 | Onboard RGB LED | Pin documented; not exercised |
| I²C sensor bus | Soft I²C3 | SCL PE0, SDA PE1 | AHT10/AHT21 at `0x38` | Yes |
| I²C sensor bus | Soft I²C2 | SCL PF1, SDA PF0 | AP3216C `0x1E`, ICM20608 `0x68`, ES8388 `0x10` | Yes |
| AP3216C interrupt | GPIO | PG13 | Onboard AP3216C | Pin documented; polling used |
| ICM20608 interrupt | GPIO | PG12 | Onboard ICM20608 | Pin documented; polling used |
| Audio codec | I²C2 | SCL PF1, SDA PF0 | ES8388 at `0x10` | Yes (address) |
| I²S3 WS | I²S3 | PA15 | ES8388 LRC | Yes (audio stream active) |
| I²S3 SCK | I²S3 | PB3 | ES8388 BCLK | Yes (audio stream active) |
| I²S3 SD | I²S3 | PB5 | ES8388 DACDAT | Yes (silence TX) |
| I²S3 ext SD | I2S3ext | PB4 | ES8388 ADCDAT | Yes (DMA receives samples) |
| I²S3 MCK | I²S3 | PC7 | ES8388 MCLK | Yes (codec operates) |
| Microphone input | Analog | CN3 (3.5 mm) | PJ-320A jack | Connector documented; not tested |
| Key 0 | GPIO | PC0 | Onboard button | No |
| Wi-Fi | RW007 SPI | TBD | Board-specific | No |
| Buzzer | GPIO/timer | TBD | TBD | No |

## Bring-up gates

1. **Partial:** MCU family/flash confirmed over SWD; board revision, markings, and printed schematic still pending.
2. **Partial:** ST-Link SWD upload/debug and HSE/PLL 168 MHz verified; memory layout and boot mode still pending.
3. **Done:** PlatformIO build, ST-Link flash, USART1 serial logging, and PF12 blink verified; the user observed the LED blinking.
4. **Done:** FreeRTOS runs five tasks; the sensor task reads the three I²C sensors once per second, and heap/stack values are logged. Worst-case latency is not yet measured.
5. **Partial:** AHT10/AHT21, AP3216C, ICM20608, I2S3/ES8388 capture, 2 s window upload/classification, RW007 Wi-Fi/TLS, and outage queue are verified. Ten real capture-to-notification trials are complete; still pending: sustained soak, display, buttons, buzzer, storage, and live-motion fixture footage; the report is finalized, and emulator plus photo-based physical demos are recorded.
