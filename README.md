# ESP32 Satellite Radar

An open-source satellite radar for the ESP32-C3 Super Mini with a 1.28" GC9A01 round display.

Using live TLE data and SGP4 orbit propagation, the device calculates and displays satellites currently visible from your location in real time.

---

## Credits

This project is based on the excellent **ESP32 Plane Radar** by **MatixYo**.

Original repository:
https://github.com/MatixYo/ESP32-Plane-Radar

Original 3D printable enclosure:
https://makerworld.com/en/models/2872376-esp32-plane-radar-live-ads-b-on-a-round-display

Many thanks to MatixYo for making the original project open source and providing such a solid foundation.

---

## About

This project started as a modification of the original ESP32 Plane Radar and has evolved into a dedicated satellite tracking device.

As I am still fairly new to C++ and embedded programming, a significant part of this project was developed with the assistance of **ChatGPT (OpenAI)**. The software combines my own ideas, testing, hardware integration and project direction with AI-assisted development.

---

## Features

- Real-time satellite tracking
- Live orbit calculation using the SGP4 propagator
- Automatic download of current TLE data
- Automatic synchronization via NTP
- Radar display with North always at the top
- Satellite labels
- Satellite direction indicators
- Automatic satellite selection
- Manual satellite selection using the BOOT button
- Satellite information panel
- Satellite visibility status
  - Visible
  - Earth's Shadow
  - Daylight
- Elevation-based radar rings
- Elevation-based satellite colors
- Automatic position updates every 2 seconds
- WiFi configuration using WiFiManager
- Local web configuration portal
- mDNS support
- LittleFS storage for downloaded TLE data

---

## Hardware

- ESP32-C3 Super Mini
- 1.28" GC9A01 Round Display (240×240)
- wires
- i recommend looking at the original        plane radar from MatixYo as he has well.   designed build instructions.
---

## Installation

### Web Installer (Recommended)

A web installer will be available for easy installation.

1. Connect your ESP32 via USB.
2. Open the web installer.
3. Click **Connect**.
4. Select your ESP32 board.
5. Click **Install**.
6. Wait until flashing has completed.

**Web Installer**

(https://web.esphome.io)



---

## First Startup

On first boot the device automatically starts the WiFi configuration portal.

Configure:

- WiFi network
- Latitude
- Longitude

After saving the configuration the device will automatically:

- synchronize the current UTC time
- download the latest satellite TLE data
- calculate all currently visible satellites

---

## Controls

**Short press**

Select the next visible satellite.

**Long press**

Reset WiFi settings and reopen the setup portal.

---

## Known Limitations

- The current WiFi configuration interface is still inherited from the original Plane Radar project and therefore contains some plane-related labels and wording.
- This is purely cosmetic and does not affect the functionality of the Satellite Radar.
- A redesigned setup interface specifically tailored to the Satellite Radar is planned for a future release.

---

## Roadmap

Planned features include:

- Plane and Sat Track in one Device
- Offline mode
- Satellite visual magnitude
- Additional satellite information
- Satellite categories
- Pass prediction (maybe)
- Improved user interface
- Dedicated satellite configuration pages

---

## License

This project follows the licensing terms of the original ESP32 Plane Radar project where applicable.

Please also refer to the original repository for licensing information.
