# FT-710 Dashboard for macOS · Yaesu FT-710 Remote Control
**Ported & Developed for macOS by**: Yury Jajitzky (**YY3BIG**)
**Based on**: [FT-710 Dashboard for Windows by M0NXD](https://github.com/M0NXD/FT-710Dashboard)
**License**: [Apache License 2.0](LICENSE) (See [NOTICE](NOTICE) and [DISCLAIMER.md](DISCLAIMER.md))

---

**Languages / Idiomas / Langues / Línguas / Lingue**:
[English](README_EN.md) | [Español](README_ES.md) | [Français](README_FR.md) | [Português](README_PT.md) | [Italiano](README_IT.md)

---

> [!CAUTION]
> **EXPERIMENTAL AMATEUR RADIO SOFTWARE NOTICE**
> This software is strictly experimental and distributed under the principles of the Amateur Radio Service. The **Control Operator** is at all times solely and exclusively responsible for the technical operation, RF output power, frequency band limits, Standing Wave Ratio (SWR), and PTT keying of their station. For initial tests, always use a **50 Ω Dummy Load**. Read the full legal terms in [DISCLAIMER.md](DISCLAIMER.md).

---

## macOS Features

* **Native Apple Silicon & Intel Build**: Pure C++20 optimized for macOS (`arm64` and `x86_64`) using Apple Clang.
* **Full CAT Control**: Bidirectional control of VFO A and B, operating modes, filter widths, RF/AF gains, transmit/receive meters, DNR, NB, and radio functions of the Yaesu FT-710 via Hamlib 4.7.2.
* **Automatic USB Port Detection**: Scans and connects automatically to the transceiver at `/dev/cu.SLAB_USBtoUART*` or `/dev/cu.usbserial*`.
* **Shared NET rigctl Server (Port 4532)**: Allows Mac ham radio software (such as **WSJT-X**, **JS8Call**, **fldigi**, **MacLoggerDX**, **RUMlogNG**) to simultaneously share CAT control of the radio.
* **Low-Latency Bidirectional Audio (CoreAudio)**: Seamless integration with the FT-710 internal USB sound card (`USB Audio CODEC`) via PortAudio and native Apple CoreAudio.
* **HTTPS Web & Mobile Server (Port 8888)**: Full remote control and audio streaming from any iPhone, iPad, Mac, or web browser on your local network.
* **Real-Time Audio Spectrum & Waterfall**: 4096-point Hann FFT rendered fluidly in JavaScript/WebGL at 60 fps.
* **FTDI FT4222H RF Scope Support**: Hardware RF spectrum capture over high-speed USB-SPI when **SCU-LAN10 = ON** is enabled on the FT-710.

---

## Installation & Compilation on macOS

### 1. Prerequisites (Homebrew)
Install the required dependencies using Homebrew:
```bash
brew install cmake hamlib portaudio openssl@3
```

### 2. Build

By default, builds without debug output for optimal performance:
```bash
cmake -B build-mac -S .
cmake --build build-mac
```

**Debug mode**: For detailed debug output (JSON construction, memory usage, etc.), enable `BUILD_DEBUG`:
```bash
cmake -B build-mac -S . -DBUILD_DEBUG=ON
cmake --build build-mac
```

The compiled binary will be placed at: `build-mac/FT710Dashboard-macOS`.

---

## Usage & Operation

### Demo Mode (Without physical radio)
To explore the interface and test features without connecting the transceiver:
```bash
./build-mac/FT710Dashboard-macOS --demo
```

### Connecting to your Yaesu FT-710
1. Connect the rear USB port of the FT-710 to your Mac.
2. Turn on the radio and run:
```bash
./build-mac/FT710Dashboard-macOS
```
The application will automatically detect the serial port. To specify a port or baud rate manually:
```bash
./build-mac/FT710Dashboard-macOS --port /dev/cu.SLAB_USBtoUART --baud 38400
```

### Command-Line Options
```text
  --demo            Run in simulation mode using the Hamlib Dummy backend
  --port <path>     Serial port path for Enhanced CAT-1 (e.g. /dev/cu.SLAB_USBtoUART)
  --baud <rate>     CAT baud rate (default: 38400)
  --passcode <code> Fixed web dashboard access passcode
  --no-browser      Do not launch web browser automatically on startup
  --help, -h        Display help message
```

---

## Web Access & Phone Audio on iPhone / iPad

  1. Upon starting the server, open in Safari: `http://localhost:8888/setup` (or
  replace `localhost` with your Mac's local IP address).
  2. Download the configuration profile and install it in iOS (**Settings &rarr;
  Profile Downloaded &rarr; Install**).
  3. Enable certificate trust in iOS (**Settings &rarr; General &rarr; About
  &rarr; Certificate Trust Settings &rarr; Enable full trust for M0NXD /
  YY3BIG**).
  4. Navigate to `https://<your-mac-ip>:8888/` for full remote control and
  bidirectional audio from your phone.

  **macOS Users (Desktop Browser):**
  If you see a "Certificate Authority Invalid" error in your browser:
  1. Open **Keychain Access**.
  2. Search for `M0NXD FT710 Local Web Root`.
  3. Double-click the certificate, expand **Trust**, and set **When using this
  certificate** to **Always Trust**.
---

## License & Attribution

This project is distributed under the **Apache License 2.0**.
Pursuant to Section 4(d) of the license:
* Any redistribution, fork, modification, or derivative work **MUST retain the `NOTICE` file** and provide prominent attribution to:
  **"Yury Jajitzky (YY3BIG) - FT-710 Dashboard for macOS"**
* Original Windows project developed by **M0NXD** (https://github.com/M0NXD/FT-710Dashboard).
