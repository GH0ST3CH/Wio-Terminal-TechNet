<p align="center">
  <img src="Assets/technet_banner.png" alt="TechNet Firmware Interface" width="900">
</p>

<br>

<h1 align="center">TechNet Firmware</h1>

<p align="center">
  <b>Advanced wireless security and utility firmware for the Seeed Studio Wio Terminal</b><br/>
  <sub>Dual-band WiFi • BLE monitoring • BadUSB payloads • Captive portal tools • On-device UI</sub>
</p>

<p align="center">
  <a href="https://github.com/GH0ST3CH/Wio-Terminal-TechNet/releases/tag/v1.0.0"><img alt="Release" src="https://img.shields.io/badge/release-v1.0.0-blue"></a>
  <a href="https://github.com/GH0ST3CH/Wio-Terminal-TechNet/blob/main/LICENSE"><img alt="License" src="https://img.shields.io/badge/license-MIT-green"></a>
  <a href="https://wiki.seeedstudio.com/Wio-Terminal-Getting-Started"><img alt="Platform" src="https://img.shields.io/badge/platform-Wio%20Terminal-blue"></a>
  <a href="https://github.com/GH0ST3CH/Wio-Terminal-TechNet/tree/main/OFFICIALTechNet"><img alt="Firmware" src="https://img.shields.io/badge/source-firmware-orange"></a>
</p>

<p align="center">
  <img src="Assets/demo.gif" alt="TechNet Demo" width="900">
</p>

---

## Overview

**TechNet** is a custom firmware platform built for the **Seeed Studio Wio Terminal**, designed to bring advanced wireless monitoring, automation, and standalone utility tools into a compact handheld device.

The firmware provides a fast **on-device interface**, **SD card integration**, wireless tooling, and standalone workflows — all directly from the Wio Terminal without requiring a companion computer once flashed.

---

## Highlights

- **Dual-band wireless support** for **2.4 GHz** and **5 GHz** workflows
- **BLE MAC Monitor** for nearby Bluetooth device observation
- **SSID Monitor** for nearby wireless network visibility
- **Wio BadUSB** with **Rubber Ducky-like syntax**
- **Captive portal utilities**
- **SD card file and payload access**
- **Fully standalone on-device UI**

---

## Controls (Wio Terminal)

Displayed in the footer inside the firmware UI:

- **UP / DOWN**: Navigate
- **LEFT / RIGHT**: Change options / scroll
- **OK (5-way press)**: Select / Run
- **LB (Left bumper button / BTN_C)**: Back / Exit

---

## Quick Flash

### Easiest install method

1. Download the latest firmware from the **Release** page  
   https://github.com/GH0ST3CH/Wio-Terminal-TechNet/releases/tag/v1.0.0

2. Connect the **Wio Terminal** using a USB-C **data** cable

3. Slide the **power switch twice quickly** to enter bootloader mode

4. A drive named:

```text
Arduino
```

will appear on your computer

5. Drag the TechNet firmware `.uf2` file onto that drive

6. The device will automatically flash and reboot into **TechNet**

---

## Firmware Source

Full firmware source is available here:  
https://github.com/GH0ST3CH/Wio-Terminal-TechNet/tree/main/OFFICIALTechNet

---

## Build From Source

### 1) Clone the repository

```bash
git clone https://github.com/GH0ST3CH/Wio-Terminal-TechNet.git
cd Wio-Terminal-TechNet
```

### 2) Install prerequisites

#### Software
- **Arduino IDE** (v1.8.x or v2.x)  
  https://www.arduino.cc/en/software

#### Board support
Add this to **Arduino IDE → Preferences → Additional Boards Manager URLs**:

```text
https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json
```

Then install:

- **Seeed SAMD Boards**

Select the board:

- **Tools → Board → Wio Terminal**

### 3) Open the firmware

Open:

```text
OFFICIALTechNet.ino
```

### 4) Build and upload

- Click **Verify**
- Click **Upload**

After upload, the device will reboot into **TechNet**.

---

## Credits & Acknowledgments

### JustCallMeKoko — ESP32 Marauder

Concepts behind the **BLE MAC Monitor** and **SSID Monitor** applications in TechNet were influenced by the wireless monitoring tools developed by **JustCallMeKoko** for the **ESP32 Marauder** project.

Project:  
https://github.com/justcallmekoko/ESP32Marauder

---

### Hak5 — USB Rubber Ducky

The **Wio BadUSB** component of TechNet uses a **Rubber Ducky-like payload syntax** inspired by the scripting approach introduced by **Hak5**.

Examples include commands such as:

```text
STRING
DELAY
DEFAULTDELAY
REPEAT
```

Project:  
https://hak5.org/products/usb-rubber-ducky

Documentation:  
https://docs.hak5.org

---

### Seeed Studio

Special thanks to **Seeed Studio** for creating the **Wio Terminal** hardware platform that makes this firmware possible.

Documentation:  
https://wiki.seeedstudio.com/Wio-Terminal-Getting-Started/

---

TechNet is an independent open-source firmware project and is **not affiliated with or endorsed by Hak5, Seeed Studio, or the ESP32 Marauder project**.

---

## Contributing

Ideas, fixes, and enhancements are welcome via **Issues** and **Pull Requests**.

---

## License

MIT — see `LICENSE`  
https://github.com/GH0ST3CH/Wio-Terminal-TechNet/blob/main/LICENSE

---

## Support the Project

If you appreciate this firmware, consider supporting ongoing development:

<p align="center">
  <a href="https://buymeacoffee.com/ghostech">
    <img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me a Coffee" height="140">
  </a>
</p>
