# Connectivity Port Plan (RW007 + TLS + FCM)

The RW007, lwIP, TLS, audio-upload, and ten-trial capture-to-notification paths are implemented and verified on hardware. This document records the path and the measured result so the work is reproducible rather than hidden in a stub.

## 1. RW007 Wi-Fi

The RT-Spark BSP provides `rw007-v2.0.1` under the RT-Thread sample projects. It is not a standalone STM32Cube driver:

- `src/spi_wifi_rw007.c` uses RT-Thread `rt_spi_device`, `rt_mempool`, `rt_mailbox`, `rt_event`, and `rt_wlan_device`.
- Its data path carries Ethernet frames (`DATA_TYPE_STA_ETH_DATA`) to an lwIP netif.
- `example/rw007_stm32_port.c` uses RT-Thread pin/IRQ and SPI device attach APIs.

The STM32Cube framework installed for this project already contains:

```text
.pio-core/packages/framework-stm32cubef4/Middlewares/Third_Party/LwIP
.pio-core/packages/framework-stm32cubef4/Middlewares/Third_Party/mbedTLS
```

### Schematic pin map (HW1.1)

Derived from `docs/../.dev-tools/downloads/rtspark/schematic.pdf`, sheet 5 (`5.stm32f407_peripherals`) and sheet 10 (`10.ethernet & rw007-wifi`). Confirm with continuity before driving the module.

| RW007 signal | MCU pin | MCU port | Notes |
|---|---|---|---|
| `WIFI_SPI_SCK` | 74 | PB13 | SPI2 SCK |
| `WIFI_SPI_MISO` | 28 | PC2 | SPI2 MISO |
| `WIFI_SPI_MOSI` | 29 | PC3 | SPI2 MOSI |
| `WIFI_SPI_CS` | 22 | PF10 | active-low chip select; BSP `RW007_CS_PIN=90` |
| `WIFI_INT` | 126 | PG11 | RW007 interrupt/BUSY |
| `WIFI_RST` | 132 | PG15 | active-low reset |
| `+3V3` / `GND` | — | — | module power |

The RW007 `CLK/RX/BOOT0` and `nCS/BOOT1` pins share the SPI clock and chip-select nets. Their reset-time levels select the module boot mode; do not drive SCK or CS during RW007 reset unless the boot strap is confirmed.

### Port sequence

1. **Confirm pins** above against the BSP `CONFIG_RW007_*` values and, if possible, with continuity from the RW007 pads to the MCU pins.
2. **SPI transport**: HAL SPI2 with DMA, full-duplex 16-byte master header exchange, chip-select control, and the INT/BUSY EXTI wake-up.
3. **Protocol layer**: port the RW007 command/response framing, sequence numbers, buffer ownership, and WLAN commands (`INIT`, `JOIN`, `GET_RSSI`, `DISCONNECT`).
4. **lwIP netif**: integrate the STM32Cube lwIP middleware, implement `low_level_output` for Ethernet frames, and feed received frames into `ethernet_input`.
5. **Credentials**: store the SSID/password and device token outside Git; use a local ignored configuration header or build-time provisioning.
6. **TLS**: mbedTLS 2.16 with a pinned local CA, hostname verification, and an explicit `ECDHE-RSA-AES128-GCM-SHA256` suite. A Caddy terminator serves TLS 1.2 on 8443 and reverse-proxies to FastAPI; no `SECLEVEL=0` override. Authenticated `POST /telemetry` and `POST /audio-windows` are verified.
7. **Failure behavior**: bounded 8-slot telemetry queue, 1 Hz sensor sampling rate-limited to one queued sample per 5 s, audio priority with telemetry fairness, retry, and post-recovery drain. A 30 s AP outage trial reconnected and resumed 200 responses; the app fault/stale state was verified separately.

The path is now implemented and hardware-verified; the port is maintained in `firmware/src/wifi_net.c` and the board/pinmap record in `docs/board-inventory.md`.

### Probe result (2026-11-13)

A boot-time probe (`firmware/src/wifi_probe.c`, built with `-D WIFI_PROBE`) established the SPI link.

Confirmed configuration:

- SPI2: SCK PB13, MISO PC2, MOSI PC3, mode 0, MSB first.
- CS: PF10, active low.
- INT/BUSY: PG11. RST: PG15.
- `INT` reads high after reset.
- With a **1 ms CS-low setup delay** at 164 kHz, the module returns a valid `slave_cmd_phase` header:
  `00 30 00 00 00 00 00 00 FE DC BA 98 76 54 32 10`.
- At 5.25 MHz without the setup delay, the response was shifted by one bit; the first clock edge was being lost.
- At 1.3 MHz with the setup delay, the command transport is stable: `GET_VSR` returns `RW007_2.1.0-a7a0d089-57`, and `JOIN` returns result `0`.

This verifies the module, wiring, reset, chip select, SPI mode, header format, command transport, version read, and AP join.

### lwIP/DHCP status

lwIP runs as a raw netif over RW007 `STA_ETH` frames. The module MAC is read with `MAC_GET` and used by lwIP. The board joins the PC-hosted WPA2 hotspot, obtains DHCP (`192.168.137.x`), exchanges ARP/TCP, completes a pinned-CA TLS 1.2 GCM handshake, and receives `HTTP/1.1 200 OK` for telemetry and 64 KB audio windows. A Caddy terminator on port 8443 reverse-proxies to FastAPI. The earlier “no Ethernet RX” fault is resolved; the remaining audio evidence gap is the controlled acoustic source, not the network path.

### Local TLS termination

`tools/Caddyfile.example` is the tested shape. Install Caddy 2.x outside the repository, copy the example, point `tls` at the local certificate/key pair (the private key stays outside Git), and run:

```powershell
caddy run --config Caddyfile
```

The certificate must contain a DNS SAN (`baby-monitor.local`) and the address the client uses. The firmware pins the matching CA and negotiates TLS 1.2 `ECDHE-RSA-AES128-GCM-SHA256`; no `SECLEVEL=0` or `VERIFY_OPTIONAL` workaround is part of the final path.

## 2. FCM

The Android app, backend installation endpoint, and FCM service code are implemented. The local Firebase project and credentials are intentionally excluded from Git:

- `android/app/google-services.json` from the Firebase project.
- `GOOGLE_APPLICATION_CREDENTIALS` service-account JSON on the backend.
- `FIREBASE_PROJECT_ID` matching the Android project.

Without those files, the app uses a local notification fallback. With them configured locally on 2026-11-14, ten backend events produced ten `push_sent=1` records and ten visible FCM notifications; see `docs/fcm-setup.md`. The local fallback remains verified 10/10 separately.

## 3. Capture-to-notification

The ten current-firmware trials are true microphone → Wi-Fi → backend → local app notifications: 10/10 delivered, median 6.27 s, max 15.82 s. The earlier API-event-to-app trials remain separate and are not relabeled.

## 4. Video and tagged release

The demonstration video must show the physical board, the app, notification/history/controls, and a Wi-Fi failure/recovery. The network path and outage recovery are working; record the video with a classroom fixture and licensed prerecorded cry audio once the acoustic source is available. No release tag or push has been made without an explicit release instruction.
