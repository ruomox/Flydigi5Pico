# Flydigi5Pico

🌍 中文 | [English](README.md)

这是一个基于硬件的低延迟 USB 协议桥接项目，旨在解决  
**飞智黑武士 5 Pro（Flydigi Vader 5 Pro）** 手柄在 **macOS** 下无法被系统识别的问题。

---

## 🎯 问题背景

飞智黑武士 5 Pro 使用了新的硬件 VID / PID 标识组合，  
而 macOS 的原生 Game Controller 框架并未对其进行适配。

结果是：  
无线接收器可以被系统识别，但手柄本身**完全无输入响应**。

macOS 不提供通用的 XInput 回退路径，一旦描述符不匹配，
设备将被系统直接忽略。

---

## 🛠 技术方案

本项目运行于 **RP2350** 微控制器（推荐并测试于  
**微雪 Waveshare RP2350-USB-A** 开发板），通过在同一芯片上同时运行
**USB Host 与 USB Device 双协议栈**，实现一个稳定、低延迟的
**外置 USB 协议桥接器**。

该设计并非传统的 USB OTG 模式切换，而是将 Host 与 Device
完全解耦并并行运行。

---

## ⚡ 架构与性能

本固件充分挖掘了 RP2350 的硬件能力，构建了一套
**双核并行的 TinyUSB 双协议栈架构（Simultaneous Dual-Stack Architecture）**，
确保输入轮询与输出传输互不阻塞。

### 1. Host / Device 双栈并行运行

不同于常规只能在 Host 或 Device 之间切换的设计，
本项目在两个核心上**同时运行 USB Host 与 USB Device 协议栈**：

- **Core 0 —— Host 协议栈**
  - 运行 **TinyUSB Host (`tusb_host`)**
  - 借助 RP2350 的 **PIO（可编程 I/O）** 实现 USB 信号层（Software PHY）
  - 以纯软件方式驱动 USB 总线，读取飞智无线接收器的数据
  - 逻辑 Host 协议栈与原生 USB 控制器完全解耦

- **Core 1 —— Device 协议栈**
  - 运行 **TinyUSB Device (`tusb_device`)**
  - 使用 RP2350 的原生 USB 控制器
  - 伪装为 **Xbox 360 有线手柄**
    （VID `0x045E` / PID `0x028E`）
  - 与 macOS 进行稳定、标准的 USB 通信

---

### 2. 物理级“硬复位”（Hard Reset）机制

第三方无线接收器在软重启后无法正确初始化，是常见问题之一。

- **问题**：  
  仅通过软件复位 USB 协议栈，无法保证接收器重新进入干净状态。

- **解决方案**：  
  固件在启动阶段实现了一次 **物理 USB 总线复位（Physical Bus Reset）**。

- **实现方式**：  
  在 USB 协议栈初始化前，通过 GPIO 直接将 **D+ / D- 线拉低**
  至 **SE0 状态**，模拟真实的“拔插”行为，强制接收器完全重置。

- **效果**：  
  接收器初始化成功率显著提升，避免随机性连接失败。

---

### 3. 自定义 XInput Host 驱动（`Flydigi5Host`）

由于飞智无线接收器**并非标准 HID 设备**，必须实现专用驱动。

- **原因**：  
  该接收器不使用 USB HID（Class `0x03`），  
  而是采用 **Vendor-Specific XInput 接口**
  （Class `0xFF`, SubClass `0x5D`）。  
  通用 HID 驱动会直接忽略该设备。

- **实现**：  
  项目实现了一个定制的 Host 类驱动 `Flydigi5Host`，用于：
  - 与该 Vendor-Specific 接口完成握手
  - 解析专有的原始数据包
  - 提取并规范化手柄输入状态

---

### 4. 无锁双核通信（Seqlock）

为避免互斥锁、消息队列带来的调度延迟，内核间通信采用
**Seqlock（序列锁）机制**：

- **写入（Core 0）**：  
  写入最新手柄状态并更新序列号，**从不阻塞**

- **读取（Core 1）**：  
  若在读取期间检测到写入冲突，则立即重试

- **结果**：  
  零阻塞、低抖动、极小缓存争用

**实测端到端物理延迟约为：2ms**

---

> **注意：**  
> 本项目仅适用于 **macOS**。  
> Windows 系统已原生支持飞智无线接收器，
> 使用本桥接方案无任何额外收益。

---

## 🖥 硬件连接说明

本项目针对 **Waveshare RP2350-USB-A** 设计，无需焊接。

- **板载 USB-A 口（PIO-USB Host）**  
  插入飞智无线接收器

- **USB-C / Micro USB 口（Native Device）**  
  连接至 Mac 电脑

---

## 🔨 编译说明

### 环境要求

- Raspberry Pi Pico SDK **2.2.0**
- CMake
- ARM GNU 工具链
- Git（需支持 submodule）

### 编译步骤

```bash
# 必须递归拉取子模块（包含 pico-pio-usb）
git clone --recursive https://github.com/ruomox/Flydigi5Pico.git
cd Flydigi5Pico

mkdir build
cd build

# 针对 RP2350（Pico 2）
cmake .. -DPICO_BOARD=pico2
make
```
生成的 `.uf2` 文件可直接拖拽烧录至开发板。

---

## ⚠️ 已知限制

- **仅限 macOS**  
  描述符伪装逻辑专为 macOS USB 与 Game Controller 框架设计。

- **设备限定**  
  目前仅针对 **飞智黑武士 5 Pro** 完成逆向与测试。

- **震动支持**  
  已完整支持，通过桥接 XInput 输出端点  
  （Endpoint `0x05`）实现回传。

---

## 📦 依赖项

- **Raspberry Pi Pico SDK**（测试版本：2.2.0）

- **pico-pio-usb**  
  以 **Git 子模块**形式引入，并固定在已验证的提交版本。

- **TinyUSB**  
  随 Pico SDK 一并提供。

---

## 🧪 测试环境

- macOS 26+
- Apple Silicon（M3 Pro）

---

## 📄 许可证

本项目基于 **Mozilla Public License 2.0[(MPL-2.0)](LICENSE)** 许可发布。

### 开源致谢

本项目构建于以下开源项目之上：

- **pico-pio-usb** — sekigon-gonnoc（MIT License）
- **TinyUSB** — hathach（MIT License）
- **Raspberry Pi Pico SDK**
