# Flydigi5Pico

🌍 English | [简体中文](./README.md)

A low-latency, hardware-based USB protocol bridge designed to force **macOS**
compatibility for the **Flydigi Vader 5 Pro** controller.

---

## 🎯 The Problem

The Flydigi Vader 5 Pro introduced a new set of hardware VID/PID signatures that
are not recognized by the native macOS Game Controller framework. As a result,
the wireless receiver can connect, but the controller remains completely
unresponsive on macOS.

macOS does not provide a generic XInput fallback, and without a matching
descriptor, the device is ignored by the system.

---

## 🛠 Technical Solution

This project runs on an **RP2350** microcontroller (tested with
**Waveshare RP2350-USB-A**) and acts as an external USB protocol bridge
between the Flydigi dongle and macOS.

It does **not** modify macOS, install drivers, or use kernel extensions.
All functionality is implemented purely as an external USB device,
based on standard USB specifications.

### Data Flow

1. **Input (USB Host)**  
   The RP2350 uses a software-defined USB Host (PIO-USB) to read raw
   **XInput-compatible** data packets from the Flydigi wireless dongle.

2. **Processing (Dual-Core IPC)**  
   Parsed controller state is transferred between cores using a
   lock-free shared memory buffer.

3. **Output (USB Device)**  
   The RP2350 enumerates as a standard **Xbox 360 Wired Controller**
   (VID `0x045E`, PID `0x028E`), which is natively supported by macOS.

> **Note:**  
> This project is intended **only for macOS**.  
> Windows already supports the Flydigi dongle natively and does not
> benefit from this bridge.

---

## ⚡ Architecture & Performance

The firmware leverages the **dual-core architecture** of the RP2350 to
ensure deterministic input polling and minimal latency.

### 1. Dual-Core Pipeline

- **Core 0 – Input Engine**
  - Runs the `pico-pio-usb` USB Host stack.
  - Polls the Flydigi receiver at a strict **1000 Hz (1 ms interval)**.
  - Parses raw packets into a normalized gamepad state structure.

- **Core 1 – Output Engine**
  - Runs the native TinyUSB **Device** stack.
  - Spoofs USB descriptors to emulate an Xbox 360 controller.
  - Handles outgoing **Rumble (Vibration)** packets from macOS.

### 2. Lock-Free IPC (Seqlock)

To avoid latency introduced by mutexes or queues, the two cores communicate
using a **Seqlock (Sequence Lock)** mechanism:

- **Write:** Core 0 writes the latest state and increments a sequence counter
  without blocking.
- **Read:** Core 1 retries immediately if a write occurs during read.
- **Result:** Zero blocking and minimal cache contention.

**Measured end-to-end latency:** ~**2 ms**.

---

## 🖥 Hardware Setup

Designed and tested for **Waveshare RP2350-USB-A**.

- **USB-A Port (PIO-USB Host):**  
  Plug the Flydigi wireless receiver here.

- **USB-C / Micro USB Port (Native Device):**  
  Connect to your Mac.

No soldering or hardware modification is required.

---

## 🔨 Build Instructions

### Requirements

- Raspberry Pi Pico SDK **2.2.0**
- CMake
- ARM GNU Toolchain
- Git (with submodule support)

### Build

```bash
# Clone with submodules (required for pico-pio-usb)
git clone --recursive https://github.com/your-username/Flydigi5Pico.git
cd Flydigi5Pico

mkdir build
cd build

# Target RP2350 (Pico 2)
cmake .. -DPICO_BOARD=pico2
make
```
The resulting `.uf2` file can be flashed directly to the board.

---

## ⚠️ Known Constraints

- **macOS Only**  
  Descriptor spoofing is designed specifically for macOS USB and  
  Game Controller frameworks.

- **Device Specific**  
  Tested only with **Flydigi Vader 5 Pro**.

- **Rumble Support**  
  Fully supported via bridging the XInput OUT endpoint (Endpoint `0x05`)  
  back to the dongle.

---

## 📦 Dependencies

- **Raspberry Pi Pico SDK** (tested with 2.2.0)

- **pico-pio-usb**  
  Included as a git submodule, pinned to a known-good commit.

- **TinyUSB**  
  Bundled with the Pico SDK.

---

## 🧪 Tested Environment

- macOS 26+
- Apple Silicon (M3 Pro)

---

## 📄 License

This project is licensed under the **Mozilla Public License 2.0 [(MPL-2.0)](LICENSE)**.

### Attribution

This project builds upon the following open-source projects:

- **pico-pio-usb** by sekigon-gonnoc (MIT License)
- **TinyUSB** by hathach (MIT License)
- **Raspberry Pi Pico SDK**
