# 🎮 StreamLight [![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-blue.svg)](https://github.com/FoggyBytes/StreamLight) [![Framework](https://img.shields.io/badge/Framework-Qt%206-green.svg)](https://www.qt.io/) [![Downloads](https://img.shields.io/github/downloads/FoggyBytes/StreamLight/total?label=Downloads&color=orange)](https://github.com/FoggyBytes/StreamLight/releases) [![Built on Moonlight](https://img.shields.io/badge/built%20on-Moonlight-blue?&logo=github)](https://github.com/moonlight-stream/moonlight-qt) 

**A Moonlight fork with [StreamTweak](https://github.com/FoggyBytes/StreamTweak) integration to manage host NIC speed before streaming**

> 🚨 This project is not affiliated with or endorsed by the Moonlight project.

> ⚠️ StreamLight is designed to be used exclusively in combination with [StreamTweak](https://github.com/FoggyBytes/StreamTweak) — Auto-Switch Ethernet speed for a stutter-free Moonlight ↔ Sunshine/Apollo experience, and more. Without StreamTweak running on the host PC, the StreamTweak-specific features will not function.

## 📖 What is StreamLight?

StreamLight is a fork of [Moonlight](https://github.com/moonlight-stream/moonlight-qt) — the open-source game streaming client — extended with native integration for [StreamTweak](https://github.com/FoggyBytes/StreamTweak), a companion tray app that automatically manages Ethernet speed on the host PC for a stutter-free streaming experience.

## ✅ Compatibility

StreamLight is currently available for **Windows only**.

## ✨ What's New in Version 1.2.0 - "The Host Metrics Update" (20/03/2026)

### 🚀 New Features
* **Host metrics in overlay**: the performance overlay now includes a **"Host Metrics (StreamTweak)"** section with real-time data from the host PC:
    * **GPU** usage % — cross-vendor (NVIDIA, AMD, Intel Arc) via PDH PerformanceCounters
    * **GPU Enc** (encoder) usage %
    * **GPU Temp** (°C) — NVIDIA only via NVML; shows N/A on non-NVIDIA systems
    * **VRAM** used / total (MB) — total shown only on NVIDIA; AMD/Intel show used only
    * **CPU** usage %
    * **Net TX** (Mbps) — host outbound network throughput
* **Graceful degradation**: the host metrics section is entirely hidden when StreamTweak is not reachable; individual metrics that are unavailable show N/A rather than a placeholder value

> StreamTweak 4.4.0 or later is required on the host PC for host metrics to appear in the overlay.

## 🛠️ Differences from Upstream Moonlight

This version of **StreamLight** includes specific integrations not found in the original Moonlight:

### 🔗 StreamTweak Integration
Right-clicking a paired host PC now exposes two additional actions:
* **Show host NIC speed**: Queries [StreamTweak](https://github.com/FoggyBytes/StreamTweak) on the host via TCP to display the current Ethernet adapter speed.
* **Set host to 1 Gbps**: Commands the host NIC to switch to 1 Gbps before connecting.
    * Includes a **10-second countdown** before the connection starts.
    * **Safety Fallback**: If no connection is made within 30 seconds, the host reverts to its original speed automatically.

### 🎨 Visual & Identity
* **Branding**: Window title changed to `StreamLight (a Moonlight fork)`.
* **Theme**: Color palette aligned with *StreamTweak* for a seamless user experience.
* **Cleanup**:
    * Removed Discord links (to avoid redirecting users to the upstream Moonlight support channels).
    * Disabled the **Auto-update checker** to prevent accidental overwrites by upstream releases.

## 🖥️ Requirements

- [StreamTweak](https://github.com/FoggyBytes/StreamTweak) must be installed and running on the **host PC** (4.4.0+ required for host metrics in the overlay)
- Windows 10 or later on the **client PC**
- A Sunshine or Apollo-compatible host

## 📝 Installation

Download the latest installer from the [Releases](../../releases) page and run it.

## 🙏 Credits

StreamLight is built on top of [Moonlight](https://github.com/moonlight-stream/moonlight-qt) by the Moonlight contributors. Full credit to the original project — without their work, StreamLight would not exist.

StreamLight is released under the [GPL v3 License](LICENSE), in accordance with the upstream Moonlight license.