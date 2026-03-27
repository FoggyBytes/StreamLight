# 🎮 StreamLight [![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-blue.svg)](https://github.com/FoggyBytes/StreamLight) [![Framework](https://img.shields.io/badge/Framework-Qt%206-green.svg)](https://www.qt.io/) [![Downloads](https://img.shields.io/github/downloads/FoggyBytes/StreamLight/total?label=Downloads&color=orange)](https://github.com/FoggyBytes/StreamLight/releases) [![Built on Moonlight](https://img.shields.io/badge/built%20on-Moonlight-blue?&logo=github)](https://github.com/moonlight-stream/moonlight-qt) 

**A Moonlight fork with [StreamTweak](https://github.com/FoggyBytes/StreamTweak) integration to manage host NIC speed before streaming**

> 🚨 This project is not affiliated with or endorsed by the Moonlight project.

> ⚠️ StreamLight is designed to be used exclusively in combination with [StreamTweak](https://github.com/FoggyBytes/StreamTweak) — Auto-Switch Ethernet speed for a stutter-free Moonlight ↔ Sunshine/Apollo experience, and more. Without StreamTweak running on the host PC, the StreamTweak-specific features will not function.

## 📖 What is StreamLight?

StreamLight is a fork of [Moonlight](https://github.com/moonlight-stream/moonlight-qt) — the open-source game streaming client — extended with native integration for [StreamTweak](https://github.com/FoggyBytes/StreamTweak), a companion tray app that automatically manages Ethernet speed on the host PC for a stutter-free streaming experience.

## ✅ Compatibility

StreamLight is currently available for **Windows only**.

## ✨ What's New in Version 2.1.0 — "The Telemetry Update" (27/03/2026)

### 🚀 New Features
* **Session telemetry reporting** — StreamLight streams real-time client-side metrics to StreamTweak during active sessions: FPS, frame drops, RTT, decode latency, and bitrate are sampled every second and transmitted in periodic batches; StreamTweak uses this data to generate a session quality report visible in the Logs tab (requires StreamTweak 5.2.0 or later on the host PC)

### Previously in 2.0.1 — "The Sort Fix"

* **App list sort order fixed**: Desktop now always appears first, Steam Big Picture second, then all other apps in alphabetical order

### Previously in 2.0.0 — "The Library Update"

### 🚀 New Features
* **New FoggyBytes icon** — a new app icon visually unifies StreamLight and StreamTweak across the FoggyBytes suite
* **Store badges on game covers**: each game synced by StreamTweak's Game Library displays a small badge in the bottom-right corner of its cover art, showing the store it belongs to — Steam, Epic Games, GOG, Ubisoft Connect, Xbox, or Battle.net; fetched live from the host via the new APPSTORES command on the TCP bridge
* **Battle.net badge**: a dedicated Battle.net badge (white icon + label on semi-transparent dark background) joins the existing badge set

### 🎨 UI Redesign
* **Unified visual identity**: the full StreamLight UI has been revised to match StreamTweak's design language — color palette, spacing, and component styling now align across both apps for a seamless paired-app experience

> StreamTweak 5.0.0 or later is required on the host PC for store badges to appear.

## 🛠️ Differences from Upstream Moonlight

This version of **StreamLight** includes specific integrations not found in the original Moonlight:

### 🔗 StreamTweak Integration
Right-clicking a paired host PC now exposes two additional actions:
* **Show host NIC speed**: Queries [StreamTweak](https://github.com/FoggyBytes/StreamTweak) on the host via TCP to display the current Ethernet adapter speed.
* **Set host to 1 Gbps**: Commands the host NIC to switch to 1 Gbps before connecting.
    * Includes a **10-second countdown** before the connection starts.
    * **Safety Fallback**: If no connection is made within 30 seconds, the host reverts to its original speed automatically.

### 🎮 Game Library — Store Badges
Each game synced from StreamTweak's Game Library displays a store badge (icon + label) in the bottom-right corner of its cover art:
* Badges are fetched from the host via the **APPSTORES** TCP command on connection
* Supported stores: **Steam**, **Epic Games**, **GOG**, **Ubisoft Connect**, **Xbox**, **Battle.net**
* Requires StreamTweak 5.0.0 or later on the host PC

### 🎨 Visual & Identity
* **New FoggyBytes icon**: app icon updated to match StreamTweak's new unified FoggyBytes identity.
* **Branding**: Window title changed to `StreamLight (a Moonlight fork)`.
* **Theme**: Color palette and UI fully aligned with *StreamTweak* for a seamless paired-app experience.
* **Cleanup**:
    * Removed Discord links (to avoid redirecting users to the upstream Moonlight support channels).
    * Disabled the **Auto-update checker** to prevent accidental overwrites by upstream releases.

### 🔧 Improvements
* **App list sort order**: Desktop always first, Steam Big Picture second, then all other apps alphabetically.

## 🖥️ Requirements

- [StreamTweak](https://github.com/FoggyBytes/StreamTweak) must be installed and running on the **host PC** (5.2.0+ for session quality reports; 5.0.0+ for store badges; 4.4.0+ for host metrics in the overlay)
- Windows 10 or later on the **client PC**
- A Sunshine or Apollo-compatible host

## 📝 Installation

Download the latest installer from the [Releases](../../releases) page and run it.

## 🙏 Credits

StreamLight is built on top of [Moonlight](https://github.com/moonlight-stream/moonlight-qt) by the Moonlight contributors. Full credit to the original project — without their work, StreamLight would not exist.

StreamLight is released under the [GPL v3 License](LICENSE), in accordance with the upstream Moonlight license.