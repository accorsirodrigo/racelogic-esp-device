# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

A single-file Arduino sketch for an **ESP32-C3** that receives real-time telemetry from **SimHub** over Serial and renders it on a **2.42" OLED display** (SSD1309 controller, 128×64px) via I2C.

## Build & Flash Commands

Using `arduino-cli` (preferred for CLI workflows):

```bash
# Compile
arduino-cli compile --fqbn esp32:esp32:esp32c3 RacelogicSimHubDevice.ino

# Upload (replace /dev/ttyUSB0 with the actual port, e.g. COM3 on Windows)
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32c3 RacelogicSimHubDevice.ino

# Monitor serial output
arduino-cli monitor -p /dev/ttyUSB0 --config baudrate=115200
```

Required board package: `esp32:esp32` (install via `arduino-cli core install esp32:esp32`).  
Required library: `U8g2` (install via `arduino-cli lib install "U8g2"`).

## Hardware Pinout

| Signal | ESP32-C3 Pin |
|--------|-------------|
| I2C SDA | 8 |
| I2C SCL | 9 |
| Button UP | 5 (INPUT_PULLUP) |
| Button DOWN | 6 (INPUT_PULLUP) |
| Button ENTER | 7 (INPUT_PULLUP) |

I2C clock is set to 400 kHz (Fast Mode) for high frame rate. The display uses the U8G2 Full Buffer mode (`_F_`) to prevent flickering.

## SimHub Serial Protocol

SimHub sends one line per update over Serial at **115200 baud**:

```
CUR_LAP;BEST_LAP;LAST_LAP;GAP_BEST;GAP_LAST;GAP_OPT;SPEED\n
```

Field sizes: lap times up to 9 chars (e.g. `1:23.456`), gap values up to 7 chars (e.g. `-1.234`), speed up to 3 chars (km/h integer).

## GitHub Actions

Use the following official Arduino actions (the old `arduino/actions/` subdirectory path is deprecated and broken):

| Purpose | Correct action |
|---------|---------------|
| Install arduino-cli | `arduino/setup-arduino-cli@v2` |
| Compile sketches | `arduino/compile-sketches@v1` |

## Architecture

The sketch is a state machine with one loop tick per display frame:

```
loop()
  ├── handleButtons()      — debounced button reads, updates state enums
  ├── processSerialData()  — accumulates chars into serialBuffer, parses on '\n' with strtok
  ├── u8g2.clearBuffer()
  ├── draw*Screen()        — renders active screen to buffer
  └── u8g2.sendBuffer()    — pushes buffer to display
```

**State enums:**
- `MainScreens` — `LAP_TIME | DELTA | SPEED | SETTINGS` (UP/DOWN navigate)
- `LapModes` — `CUR | BEST | LAST` (ENTER cycles within LAP_TIME screen)
- `GapModes` — `G_BEST | G_LAST | G_OPT` (ENTER cycles within DELTA screen)
- `editMode` flag — activated by ENTER on SETTINGS; UP/DOWN adjust brightness by ±15, ENTER confirms

**Delta bar logic** (`drawDeltaScreen`): gap value maps ±2.0 s to 0–64 px; negative grows left of center, positive grows right.
