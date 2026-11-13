# Product

<!-- impeccable:product-schema 1 -->

## Platform

android

## Stack

Native Kotlin Android app with Jetpack Compose and Material 3; STM32Cube HAL + FreeRTOS firmware built through PlatformIO; local FastAPI/SQLite backend. A small browser preview is used inside VS Code during UI development.

## Users

The primary user is a caregiver checking the classroom baby-monitor prototype at a glance and reviewing alerts and recent readings.

## Product Purpose

Show environmental readings, likely cry events, device/network health, and event history from the RT-Spark prototype. Success means a caregiver can quickly understand the current monitoring state, notice stale or faulted data, and acknowledge an alert.

## Positioning

An educational, measured prototype that demonstrates embedded sensing, cry classification, secure data exchange, and caregiver notification. It is not a medical or safety-certified monitor and does not replace adult supervision.

## Operating Context

The system is evaluated with an RT-Spark board, Android device, local workstation backend, classroom test fixture, and licensed prerecorded audio. The board is connected and running the firmware: console, 168 MHz clock, three onboard sensors, ES8388/I2S audio, and live microphone capture are verified. Wi-Fi, display, buttons, buzzer, and end-to-end notification trials remain to be completed.

## Capabilities and Constraints

- Android dashboard, event history, stale-data and fault states, alert acknowledgement, local alert settings, and notifications.
- Firmware uses STM32Cube HAL/LL and FreeRTOS through PlatformIO; RT-Thread OS/Studio, Arduino, and ESP-IDF are excluded.
- Use an external microphone for audio capture. Do not claim infant-presence, breathing, or medical-condition detection.
- Preserve privacy by transmitting only bounded triggered clips for inference and deleting them after processing.
- Keep missing sensors, unavailable services, and lost events visible as faults.

## Brand Commitments

The caregiver app should feel friendly and cartoonish, with gentle animations. Keep alert meaning, numeric readings, and device health immediately clear.

## Evidence on Hand

The assignment PDF is the requirements source. Device readings, live microphone audio, a 2 s activity-gated window, heap/stack figures, and I2S timing are available. The board pinout is partially verified; classifier accuracy, notification trials, Wi-Fi recovery, display/controls, and report/video evidence are still pending. Any preview readings must be labeled as demo data.

## Product Principles

- Make monitoring state understandable at a glance.
- Make data age and device faults explicit.
- Use playful character without obscuring status or alerts.
- Keep audio bounded, protected, and short-lived.
- Present measured results and limitations honestly.

## Accessibility & Inclusion

Use readable contrast, scalable text, screen-reader labels, clear non-color status cues, 48 dp native touch targets, and honor reduced-motion settings.
