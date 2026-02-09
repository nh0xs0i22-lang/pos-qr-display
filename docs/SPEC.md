# POS QR Display – Firmware Specification

## Hardware
- MCU: ESP32-S3 with PSRAM
- Flash: 16MB
- PSRAM: Octal, 80MHz
- Display: ST7701 RGB LCD
- Resolution: 480x480
- Color depth: RGB565
- Touch: GT911 (optional)

## Software
- Framework: ESP-IDF 5.5
- UI Library: LVGL
- Language: C
- No ESPHome
- No web rendering
- No Arduino framework

## Core Features
1. MQTT-based control
2. Display QR code immediately when MQTT command received
3. Idle screensaver when no QR shown
4. First screensaver: Flip clock (modern style)
5. Result screen (success / failed)

## UI States
- IDLE (screensaver)
- QR_DISPLAY
- RESULT

## MQTT
- ESP subscribes to MQTT topics
- Payload is JSON
- QR display has higher priority than screensaver

## Performance Rules
- No full screen redraw
- Use PSRAM framebuffer
- No dynamic malloc in render loop
- QR code must be integer-scaled
