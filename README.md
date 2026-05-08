# RacelogicSimHubDevice

ESP32-C3 device that receives real-time telemetry from SimHub over Serial and displays it on a 2.42" OLED screen (SSD1309, 128×64px).

## Hardware

| Component | Details |
|-----------|---------|
| MCU | ESP32-C3 |
| Display | 2.42" OLED SSD1309 128×64 (I2C) |
| I2C pins | SDA=8, SCL=9 @ 400kHz |
| Button UP | Pin 5 |
| Button DOWN | Pin 6 |
| Button ENTER | Pin 7 |

## Screens

Navigate with **UP/DOWN**. **ENTER** cycles sub-modes or enters edit:

| Screen | ENTER action |
|--------|-------------|
| Lap Time | Cycles Current → Best → Last lap |
| Delta | Cycles Gap Best → Gap Last → Gap Optimal |
| Speed | — |
| Settings | Enters brightness edit mode (UP/DOWN adjust, ENTER confirms) |

## SimHub Setup

In SimHub, create a custom serial device at **115200 baud** sending:

```
[CUR_LAP];[BEST_LAP];[LAST_LAP];[GAP_BEST];[GAP_LAST];[GAP_OPT];[SPEED]\n
```

Example:
```
1:23.456;1:22.789;1:24.012;-0.667;+0.345;-1.234;187\n
```

## Build & Flash

Requires [arduino-cli](https://arduino.github.io/arduino-cli/), `esp32:esp32` board package, and `U8g2` library.

```bash
# First time setup
arduino-cli config init
arduino-cli config set board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32
arduino-cli lib install "U8g2"

# Compile
arduino-cli compile --fqbn esp32:esp32:esp32c3 RacelogicSimHubDevice.ino

# Flash (adjust port)
arduino-cli upload -p COM3 --fqbn esp32:esp32:esp32c3 RacelogicSimHubDevice.ino
```

## CI

| Trigger | Workflow | Resultado |
|---------|----------|-----------|
| Push em `main` | `build.yml` | Compila e sobe firmware como artifact |
| Comentar `/build-dev` num PR | `pr-build.yml` | Compila o branch do PR e posta link do artifact no comentário |
