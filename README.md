# 🎮 StreamLight [![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-blue.svg)](https://github.com/FoggyBytes/StreamLight) [![Framework](https://img.shields.io/badge/Framework-Qt%206-green.svg)](https://www.qt.io/) [![Downloads](https://img.shields.io/github/downloads/FoggyBytes/StreamLight/total?label=Downloads&color=orange)](https://github.com/FoggyBytes/StreamLight/releases) [![Built on Moonlight](https://img.shields.io/badge/built%20on-Moonlight-blue?&logo=github)](https://github.com/moonlight-stream/moonlight-qt) 

**A Moonlight fork with [StreamTweak](https://github.com/FoggyBytes/StreamTweak) integration to manage host NIC speed before streaming**

> 🚨 This project is not affiliated with or endorsed by the Moonlight project.

> ⚠️ StreamLight is designed to be used exclusively in combination with [StreamTweak](https://github.com/FoggyBytes/StreamTweak) — Auto-Switch Ethernet speed for a stutter-free Moonlight ↔ Sunshine/Apollo experience, and more. Without StreamTweak running on the host PC, the StreamTweak-specific features will not function.

## 📖 What is StreamLight?

StreamLight is a fork of [Moonlight](https://github.com/moonlight-stream/moonlight-qt) — the open-source game streaming client — extended with native integration for [StreamTweak](https://github.com/FoggyBytes/StreamTweak), a companion tray app that automatically manages Ethernet speed on the host PC for a stutter-free streaming experience.

## ✅ Compatibility

StreamLight is currently available for **Windows only**.

## ✨ What's New in Version 1.1.0 - "The Overlay Update" (18/03/2026)

### 🚀 New Features
* **Performance Overlay Redesign**: Restyled to match the *StreamTweak* aesthetic.
    * Dark semi-transparent grey background with white text using the **RobotoMono** font.
    * **Auto-Width**: The overlay box now fits content precisely (no more excess empty space).
* **Toggle Hotkeys**: Toggle the performance overlay at any time during streaming:
    * ⌨️ **Keyboard**: `Ctrl` + `Alt` + `O`
    * 🎮 **Gamepad**: `Select` + `L1` + `R1` + `X`
    * *Added a hotkey hint in Settings next to the toggle for quick reference.*
* **Latency Monitoring**: The **"Host processing latency"** row is now always visible. It displays `N/A` if the host does not report a value, ensuring a consistent layout.

### 🔧 General Changes
* **Codebase Optimization**: Removed all non-English localizations (translation files, Language enum, and selector). The app is now **English-only**, resulting in a lighter and faster-loading binary. 
    * *Note: Localization support may be reintroduced in the future once the planned feature set reaches full stability.*

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

- [StreamTweak](https://github.com/FoggyBytes/StreamTweak) must be installed and running on the **host PC**
- Windows 10 or later on the **client PC**
- A Sunshine or Apollo-compatible host

## 📝 Installation

Download the latest installer from the [Releases](../../releases) page and run it.

## 🙏 Credits

StreamLight is built on top of [Moonlight](https://github.com/moonlight-stream/moonlight-qt) by the Moonlight contributors. Full credit to the original project — without their work, StreamLight would not exist.

StreamLight is released under the [GPL v3 License](LICENSE), in accordance with the upstream Moonlight license.