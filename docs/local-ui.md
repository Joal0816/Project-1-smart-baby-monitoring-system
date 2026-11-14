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
| Red LED | PF12 | active low; boot self-test and audio low band |
| Blue LED | PF11 | active low; boot self-test, audio high band, and unacknowledged cry alert |

The LCD is the onboard 240×240 ST7789V3 panel documented by the Spark-1 user manual and initialized by `firmware/src/display.c`. The UI runs in the existing LocalUI FreeRTOS task; no additional task or dynamic allocation is used.

## Behavior

- **Status page:** temperature, humidity, light, proximity, Wi-Fi IP, audio alert, mute state.
- **Network page:** RW007 connection, IP, TLS 1.2 pinned CA, 16 kHz audio, backend port.
- **Controls page:** local key reference and safety statement.
- **KEY0:** clear/acknowledge the local cry alert and return to Status.
- **KEY1:** toggle buzzer mute.
- **KEY2:** cycle pages.
- **WK_UP:** 100 ms test beep.
- A confirmed backend audio response (`"confirmed":true`) sets a local cry alert. The blue LED and buzzer activate until KEY0 clears it; KEY1 mutes the buzzer. The blue LED also blinks for an unacknowledged alert.
- **Audio-reactive LEDs:** the AudioTask estimates the dominant frequency from hysteresis zero crossings of the mean-removed 16 kHz mono half-buffer. RMS `>=25` and peak `>=80` enable the meter; the LED state is held for 300 ms so short sounds remain visible. Below 800 Hz the red LED is on; 800–1199 Hz drives red+blue (magenta); 1200 Hz and above drives blue. The LEDs are the verified red PF12 and blue PF11 channels; no green LED pin is exposed by the board documentation.
- **Boot self-test:** both LEDs are on for 1.5 s after reset, before the audio meter takes over.

The alert is local-only. It does not acknowledge the backend event; the app remains the caregiver record. The LCD and buzzer are not safety-certified annunciators.

## Build and flash

```bash
.venv/Scripts/pio.exe run -e rtspark_f407zg_hsi
.venv/Scripts/pio.exe run -e rtspark_f407zg_hsi -t upload
```

## Current evidence

- Build: RAM `74,108 B`, flash `145,192 B` (audio-reactive UI build with LED hold and self-test).
- Serial boot: `display: ST7789 240x240 ready`; `ui: display=ready; buttons and buzzer active`.
- Audio log includes `freq=...`; a 200 Hz laptop tone produced estimates of 125–500 Hz, confirming the meter path is live. High tones are attenuated by the laptop speaker/onboard microphone at distance, so bring the source close to the board for the blue band. The zero-crossing estimate is coarse by design.
- Codec ADC gain was reduced from `0xBB` to `0x22` because the old setting saturated the meter at normal room levels.
- FreeRTOS after UI: heap minimum `8,424 B`; stack high-water `92/234/142/354/113` words; LocalUI wake delay `2 ms` in the sampled run.
- Human check at the fixture: the display rendered the status page and changed to `BUZZER MUTED` after the mute control; the WK_UP test produced an audible beep. Physical page and mute behavior confirmed on 2026-11-14.
