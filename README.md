# ESP32 ePaper Desk Dashboard

![Desk Dashboard Setup](images/3ddeskprint.png)

A customizable desk dashboard for ESP32 boards and 7.5" ePaper displays. It combines local status widgets, cloud-fetched data, and optional Zigbee automation in a single low-power display.

## Overview

This project is designed for a desk or workshop display that can show:

- time and date
- Google Calendar events
- weather forecast
- stock prices
- parcel tracking
- Proxmox server status
- Bambu Lab printer status
- MakerWorld statistics
- optional Zigbee automation

It supports configurable widget layout, cached data for offline rendering, and partial refresh on supported black-and-white ePaper panels.

## Highlights

- Multi-widget ePaper dashboard
- Remote configuration page on first boot
- Cached layout and widget data in Preferences
- Configurable widget refresh intervals
- Optional ESP32-C6 Zigbee support
- Support for prebuilt firmware and local compilation

## Hardware

Minimum setup:

- ESP32 board
- 7.5" GxEPD2-compatible ePaper display
- Wi-Fi network

Optional:

- battery-powered setup
- Bambu Lab printer via MQTT
- Zigbee switch for automation

Boards used during development:

- ESP32: Lolin D32
- ESP32-C6: DFRobot FireBeetle 2 ESP32-C6

Important notes:

- most color ePaper displays do not support partial refresh
- some large panels can take 20+ seconds for a full refresh

## Pin Reference

ESP32-C6:

- `EPD_CS=1`
- `EPD_DC=8`
- `EPD_RST=14`
- `EPD_BUSY=7`
- `EPD_SCK=23`
- `EPD_MOSI=22`
- `PIN_DISPLAYPOWER=4`
- `BAT_PIN=0`
- `DEMO_BUTTON=GPIO_NUM_2`

ESP32:

- `EPD_CS=15`
- `EPD_DC=27`
- `EPD_RST=26`
- `EPD_BUSY=25`
- `EPD_SCK=13`
- `EPD_MOSI=14`
- `PIN_DISPLAYPOWER=4`
- `BAT_PIN=35`

Adjust the board and display configuration in [configure.h](/Users/nasoni/ESP32-eInk-Dashboard/configure.h) and [ESP32-eInk-Dashboard.ino](/Users/nasoni/ESP32-eInk-Dashboard/ESP32-eInk-Dashboard.ino) if your hardware differs.

## Build Options

### Arduino IDE

Open [ESP32-eInk-Dashboard.ino](/Users/nasoni/ESP32-eInk-Dashboard/ESP32-eInk-Dashboard.ino), select the correct ESP32 board, then compile and upload as usual.

For Zigbee on ESP32-C6:

- set `USE_ZIGBEE` to `1` in [configure.h](/Users/nasoni/ESP32-eInk-Dashboard/configure.h)
- use an ESP32-C6 board
- select a Zigbee-capable partition scheme and Zigbee mode in the board menu

### arduino-cli

The repo includes [sketch.yaml](/Users/nasoni/ESP32-eInk-Dashboard/sketch.yaml) profiles.

Standard ESP32 build:

```bash
arduino-cli compile --profile esp32_default /Users/nasoni/ESP32-eInk-Dashboard
```

ESP32-C6 Zigbee build:

```bash
arduino-cli compile --profile esp32c6_zigbee /Users/nasoni/ESP32-eInk-Dashboard
```

If you prefer a direct command, this Zigbee build works:

```bash
arduino-cli compile \
  --fqbn esp32:esp32:esp32c6:PartitionScheme=custom,ZigbeeMode=zczr \
  /Users/nasoni/ESP32-eInk-Dashboard \
  -e
```

Why Arduino IDE and `arduino-cli` can behave differently:

- Arduino IDE remembers board-menu selections in the GUI
- `arduino-cli` needs those board options passed explicitly
- Zigbee on ESP32-C6 requires both the correct partition scheme and Zigbee mode

## Prebuilt Firmware

Prebuilt merged binaries are available in the project releases:

- [ESP32-eInk-Dashboard releases](https://github.com/VoIPshare/ESP32-eInk-Dashboard/releases)

To flash them from a browser, you can use:

- [espboards.dev firmware programmer](https://www.espboards.dev/tools/program/)

Use address `0x0000` when flashing the merged image.

## First Boot and Device Setup

After flashing:

1. power on the device
2. connect to the setup Wi-Fi network: `Dashbboard-Setup`
3. open `http://192.168.4.1`
4. fill in the configuration page


![Desk Dashboard Setup](images/WebConfig.png)

The setup page can store:

- Wi-Fi SSID and password
- Google Script ID
- MQTT IP, port, username, and password
- optional Zigbee-related settings when Zigbee firmware is enabled

These settings are stored on the device. Depending on upload and erase settings, they may be reset after flashing new firmware.

## Zigbee Notes

Zigbee support is intended for ESP32-C6 builds.

Current behavior:

- Zigbee control follows the Bambu aux fan state
- when the aux fan turns on, the Zigbee switch is turned on
- when the aux fan turns off, the Zigbee switch is turned off

Because printer status is refreshed periodically, the Zigbee off action can happen about 1 to 5 minutes after the fan stops.

## Google Apps Script

The firmware expects a Google Apps Script web app that acts as a central data source.

Supported data:

- parcel tracking
- calendar events
- stocks
- layout configuration
- Proxmox data
- weather
- MakerWorld data

### Deployment

1. create a new Google Apps Script project
2. add your script
3. deploy it as a Web App
4. copy the published URL

The published URL looks like:

```text
https://script.google.com/macros/s/<SCRIPT_ID>/exec
```

The firmware only needs the `<SCRIPT_ID>` portion. You enter that value on the device setup page, not in the source code.

### Spreadsheet Setup

The easiest setup is to use the bundled spreadsheet template:

1. create a new empty Google spreadsheet in Google Drive
2. import [ESP32_Import.ods](/Users/nasoni/ESP32-eInk-Dashboard/googleScripts/ESP32_Import.ods)
3. rename the spreadsheet to `ESP32Dashboard`

The script looks for a spreadsheet with that exact name in Google Drive.

The imported file includes the expected sheets and starter structure for:

- `Stocks`
- `Tracking`
- `Layout`
- `ListCarriers`

If you prefer a manual setup, sample CSV files are also included in [googleScripts](/Users/nasoni/ESP32-eInk-Dashboard/googleScripts).

### Layout Tips

Clock widget:

- put the timezone string in the `Extra1` field of the clock row
- example:

```text
EST5EDT,M3.2.0/2:00:00,M11.1.0/2:00:00
```

Weather widget:

- `Extra1`: latitude
- `Extra2`: longitude

Example:

- `Extra1 = 40.712`
- `Extra2 = -74.006`

## Fonts

Font tooling and conversion notes are documented in [fonts/README.md](/Users/nasoni/ESP32-eInk-Dashboard/fonts/README.md).

That folder includes:

- conversion scripts
- glyph lists
- Material Design icon setup
- ePaper font optimization notes

Important:

- glyph order must remain consistent

## 3D-Printed Case

The case model is available here:

- [MakerWorld enclosure model](https://makerworld.com/en/models/2443888-epaper-dashboard-7-5-with-esp32-desktop-version)

## CI Builds

GitHub Actions can build firmware for:

- ESP32
- ESP32-C6
- ESP32-C6 with Zigbee

The workflow uses the repo [partitions.csv](/Users/nasoni/ESP32-eInk-Dashboard/partitions.csv) and can inject a firmware version string that is shown on the device near the battery indicator.

## Project Structure

Key files:

- [ESP32-eInk-Dashboard.ino](/Users/nasoni/ESP32-eInk-Dashboard/ESP32-eInk-Dashboard.ino): main sketch, display update flow, setup portal
- [configure.h](/Users/nasoni/ESP32-eInk-Dashboard/configure.h): board options, feature flags, icon constants
- [fetchAllInfo.cpp](/Users/nasoni/ESP32-eInk-Dashboard/fetchAllInfo.cpp): JSON parsing and widget rendering
- [bambulab.cpp](/Users/nasoni/ESP32-eInk-Dashboard/bambulab.cpp): printer status and MQTT fetch
- [dash_zigbee.cpp](/Users/nasoni/ESP32-eInk-Dashboard/dash_zigbee.cpp): Zigbee integration
- [googleScripts](/Users/nasoni/ESP32-eInk-Dashboard/googleScripts): Apps Script samples and CSV examples

## Licenses and Attribution

Material Design Icons:

- source: [materialdesignicons.com](https://materialdesignicons.com/)
- license: Apache 2.0

Montserrat:

- source: [Google Fonts](https://fonts.google.com/specimen/Montserrat)
- license: SIL Open Font License 1.1

HighSpeed:

- source: [dafont.com/high-speed.font](https://www.dafont.com/high-speed.font)
- license: free for personal use, commercial use requires permission

## Contributing

Contributions are welcome. Good areas for improvement include:

- new widgets
- layout and UI polish
- performance tuning
- network and parsing robustness
- documentation

Open an issue or submit a pull request if you want to contribute.
