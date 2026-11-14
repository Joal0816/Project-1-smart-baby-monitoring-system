# Local hardware UI

## Pin map

| Function | MCU pin | Notes |
|---|---|---|
| LCD data D0–D7 | PD14, PD15, PD0, PD1, PE7–PE10 | ST7789 8080 parallel, FSMC bank 3 |
| LCD /OE, /WE, /CS | PD4, PD5, PG10 | FSMC NOE, NWE, NE3 |
| LCD DC/RS | PD13 | FSMC A18 |
| LCD reset | PD3 | active low |
| LCD backlight | PF9 | high enables backlight |
| KEY0 | PC0 | active low, internal pull-up |
| KEY1 | PC1 | active low, internal pull-up |
| KEY2 | PC4 | active low, internal pull-up |
| WK_UP | PC5 | active low, internal pull-up |
| Buzzer | PB0 | active high |
| Red LED | PF12 | active low, 500 ms heartbeat |
| Blue LED | PF11 | active low, on for unacknowledged cry alert |

The LCD is the onboard 240×240 ST7789V3 panel documented by the Spark-1 user manual and initialized by `firmware/src/display.c`. The UI runs in the existing LocalUI FreeRTOS task; no additional task or dynamic allocation is used.

## Behavior

- **Status page:** temperature, humidity, light, proximity, Wi-Fi IP, audio alert, mute state.
- **Network page:** RW007 connection, IP, TLS 1.2 pinned CA, 16 kHz audio, backend port.
- **Controls page:** local key reference and safety statement.
- **KEY0:** clear/acknowledge the local cry alert and return to Status.
- **KEY1:** toggle buzzer mute.
- **KEY2:** cycle pages.
- **WK_UP:** 100 ms test beep.
- A confirmed backend audio response (`"confirmed":true`) sets a local cry alert. The blue LED and buzzer activate until KEY0 clears it; KEY1 mutes the buzzer.
- **Audio-reactive LEDs:** the AudioTask estimates the dominant frequency from zero crossings of the 16 kHz mono half-buffer. RMS `>=80` and peak `>=150` enable the meter. Below 500 Hz the red LED is on; 500–1999 Hz drives red+blue (magenta); 2000 Hz and above drives blue. The LEDs are the verified red PF12 and blue PF11 channels; no green LED pin is exposed by the board documentation.

The alert is local-only. It does not acknowledge the backend event; the app remains the caregiver record. The LCD and buzzer are not safety-certified annunciators.

## Build and flash

```bash
.venv/Scripts/pio.exe run -e rtspark_f407zg_hsi
.venv/Scripts/pio.exe run -e rtspark_f407zg_hsi -t upload
```

## Current evidence

- Build: RAM `74,108 B`, flash `145,100 B` (audio-reactive UI build).
- Serial boot: `display: ST7789 240x240 ready`; `ui: display=ready; buttons and buzzer active`.
- Audio log now includes `freq=...`; a 1 kHz laptop tone produced an approximate 625–1562 Hz estimate, confirming the meter path is live and frequency-dependent (zero-crossing estimate is coarse).
- FreeRTOS after UI: heap minimum `8,424 B`; stack high-water `92/234/142/354/113` words; LocalUI wake delay `2 ms` in the sampled run.
- Human check at the fixture: the display rendered the status page and changed to `BUZZER MUTED` after the mute control; the WK_UP test produced an audible beep. Physical page and mute behavior confirmed on 2026-11-14.
