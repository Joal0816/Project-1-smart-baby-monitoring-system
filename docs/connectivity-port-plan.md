# Connectivity Port Plan (RW007 + TLS + FCM)

The remaining blockers are external to the code already written. This document records the concrete porting path so the work is reproducible rather than hidden in a stub.

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
6. **TLS**: integrate mbedTLS (already in the framework), pin the local development CA, and send authenticated `POST /telemetry` and `POST /audio-windows`. Reject invalid certificates.
7. **Failure behavior**: exponential reconnect, bounded telemetry/audio queues, stale/fault UI, and post-recovery drain.

This is a multi-stage firmware port; it cannot be completed or verified in one edit without the board pinout and an AP to test against.

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

lwIP is integrated with a raw netif over RW007 `STA_ETH` frames. The module MAC is read with `MAC_GET` and used by lwIP. The board joins both `PLDT_2.4GHZ` (-50 dBm) and a PC-hosted WPA2 hotspot (-42 dBm), and receives CONNECT events. In both cases DHCP/ARP/TCP frames are transmitted but no Ethernet frames are received. Static `192.168.137.50` on the hotspot also gets no ARP reply. SPI command responses still work, so the remaining fault is the RW007 Ethernet RX/data-path delivery, not association, lwIP TX, or AP security. TLS/HTTP is implemented but unreached.

## 2. FCM

The Android app, backend installation endpoint, and FCM service code exist. Real push requires files that are intentionally absent from Git:

- `android/app/google-services.json` from the Firebase project.
- `GOOGLE_APPLICATION_CREDENTIALS` service-account JSON on the backend.
- `FIREBASE_PROJECT_ID` matching the Android project.

Without those, the app uses a local notification fallback for newly observed events. The fallback was verified 10/10 with a median API-event-to-app latency of 14.18 s.

## 3. Capture-to-notification

The 10 local notification trials measured API event → app notification, not microphone → Wi-Fi → backend → push. True capture-to-notification requires items 1 and 2 above plus a final audio-window upload path.

## 4. Video and tagged release

The demonstration video must show the physical board, the app, notification/history/controls, and a Wi-Fi failure/recovery. It should be recorded after item 1 is working. No commit, push, or tag has been made without an explicit release instruction.
