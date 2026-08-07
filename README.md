# Sky Radar

Sky Radar is an open-source ESP32-based sky tracking device for the ESP32-C3 Super Mini with a 1.28" GC9A01 round display.

I made this using Codex here and there. I'm still new to coding, but have a lot of ideas. Codex helped me get this Project up and running, an i learned a lot about coding :)

Version 2.0 expands the original concept into a combined aircraft and satellite radar. It can display live aircraft positions and visible satellites from your location in real time, all on the same compact display.

> **Deutsche Anleitung:** [Funktionen, Einrichtung und Bedienung](BEDIENUNGSANLEITUNG.md)

---

## Credits

This project was forked from the excellent **ESP32 Plane Radar** by **MatixYo** and builds on its hardware and UI foundation.

Original repository:
https://github.com/MatixYo/ESP32-Plane-Radar

Original 3D printable enclosure:
https://makerworld.com/en/models/2872376-esp32-plane-radar-live-ads-b-on-a-round-display

Many thanks to MatixYo for making the original project open source and providing a strong foundation.

---

## About

Sky Radar started as a modification of the original ESP32 Plane Radar project and has evolved into a more advanced dual-mode tracking device.

This version 2.0 introduces:
- live aircraft radar using ADS-B data
- satellite tracking using TLE data and SGP4 orbit propagation
- improved UI and interaction flow
- setup and operation improvements for everyday use

A significant part of this project was written with the help of **CODEX**. The software combines my own ideas, testing, hardware integration, and project direction with AI-assisted development.

---

## Features

- Real-time aircraft tracking from adsb.fi
- Real-time satellite tracking using live TLE data
- SGP4-based orbit propagation
- Automatic clock synchronization via NTP
- Radar display with North always at the top
- Aircraft and satellite labels
- Movement indicators for detected objects
- Automatic and manual selection of targets
- Aircraft and satellite information panels
- Elevation-based radar rings
- Elevation-based object colors
- Automatic updates every few seconds
- Wi-Fi configuration portal
- Local web configuration setup
- LittleFS storage for downloaded TLE data
- Offline support for the satellite radar view

---

## Hardware

- ESP32-C3 Super Mini
- 1.28" GC9A01 round display (240×240)
- Wires
- Recommended reference: the original ESP32 Plane Radar project by MatixYo for build inspiration and layout ideas

---

## Installation

### Web Tool

Download the latets Firmware and use an Online tool (esphome for example) for uploading the Firmware 
to the ESP

### First Startup

On first boot the device automatically starts the Wi-Fi configuration portal.

Configure:
- Wi-Fi network
- Latitude
- Longitude

After saving the configuration the device will automatically:
- synchronize the current UTC time
- download the latest satellite TLE data
- calculate the currently visible satellites

---

## Controls

**Short press**
Select the next visible target.

**Long press**
Reset Wi-Fi settings and reopen the setup portal.

---

## Known Limitations

- The aircraft radar requires an internet connection for live updates.
- The satellite radar can work offline using stored TLE data, but accuracy depends on correct time and location.

---

## Roadmap

Planned improvements include:
- further UI refinements
- improved setup experience
- more detailed satellite information
- pass prediction support
- expanded display options

---

## License

This project follows the licensing terms of the original ESP32 Plane Radar project where applicable.

Please also refer to the original repository for licensing information.

