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

## 🛠 Technical Architecture

This project runs on the **RP2350** microcontroller (recommended and tested on  
the **Waveshare RP2350-USB-A** board) and implements a stable, low-latency
**external USB protocol bridge** by running **USB Host and USB Device stacks
simultaneously on the same chip**.

Unlike traditional USB OTG designs that switch between Host *or* Device modes,
this architecture fully decouples the two roles and executes them in parallel.

---

## ⚡ Architecture & Performance

The firmware fully exploits the RP2350’s hardware capabilities to build a
**dual-core, simultaneous TinyUSB dual-stack architecture**, ensuring that
input polling is never blocked by output transmission.

### 1. Simultaneous Host / Device Dual-Stack Operation

Unlike conventional designs that can only operate as a USB Host or Device at
any given time, this project runs **both protocol stacks concurrently** on
separate cores:

- **Core 0 — Host Stack**
  - Runs the **TinyUSB Host stack (`tusb_host`)**
  - Uses the RP2350’s **PIO (Programmable I/O)** to implement the USB signaling
    layer (Software PHY)
  - Drives the USB bus entirely in software to communicate with the Flydigi
    wireless receiver
  - The logical Host stack is completely decoupled from the native USB
    controller

- **Core 1 — Device Stack**
  - Runs the **TinyUSB Device stack (`tusb_device`)**
  - Uses the RP2350’s native USB controller
  - Enumerates as a **wired Xbox 360 controller**
    (VID `0x045E`, PID `0x028E`)
  - Communicates with macOS through standard, natively supported USB interfaces

---

### 2. Physical “Hard Reset” Mechanism

A common issue with third-party wireless dongles is unreliable initialization
after a soft reboot.

- **Problem**  
  Resetting the USB protocol stack in software alone does not guarantee that the
  dongle returns to a clean, known state.

- **Solution**  
  The firmware performs a **physical USB bus reset** during startup.

- **Implementation**  
  Before initializing the USB stack, the firmware directly drives the **D+ / D−**
  lines low via GPIO, forcing an **SE0 state**. This behavior mimics a real
  unplug/replug cycle and forces the dongle to fully reset.

- **Result**  
  Significantly improved initialization reliability and elimination of random
  connection failures.

---

### 3. Custom XInput Host Driver (`Flydigi5Host`)

A custom Host driver is required because the Flydigi wireless receiver is **not
a standard HID device**.

- **Reason**  
  The receiver does not use the USB HID class (Class `0x03`). Instead, it exposes
  a **Vendor-Specific XInput interface**
  (Class `0xFF`, SubClass `0x5D`).  
  Generic HID drivers will ignore this device entirely.

- **Implementation**  
  The project implements a dedicated Host class driver, `Flydigi5Host`, which:
  - Performs the required handshake with the Vendor-Specific interface
  - Parses the proprietary raw data packets
  - Extracts and normalizes controller input state

---

### 4. Lock-Free Dual-Core Communication (Seqlock)

To avoid latency introduced by mutexes or message queues, inter-core
communication uses a **Seqlock (Sequence Lock)** mechanism:

- **Write (Core 0)**  
  Writes the latest controller state and updates the sequence counter without
  ever blocking

- **Read (Core 1)**  
  Retries immediately if a write occurs during a read operation

- **Result**  
  Zero blocking, low jitter, and minimal cache contention

**Measured end-to-end physical latency: ~2 ms**

---

> **Note:**  
> This project is intended **only for macOS**.  
> Windows provides native support for the Flydigi wireless receiver, and this
> bridge offers no additional benefit on that platform.

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
git clone --recursive https://github.com/ruomox/Flydigi5Pico.git
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
