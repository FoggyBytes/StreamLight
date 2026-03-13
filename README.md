

# 🎮 StreamLight [![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-blue.svg)](https://github.com/FoggyBytes/StreamLight) [![Framework](https://img.shields.io/badge/Framework-Qt%206-green.svg)](https://www.qt.io/) [![Downloads](https://img.shields.io/github/downloads/FoggyBytes/StreamLight/total?label=Downloads&color=orange)](https://github.com/FoggyBytes/StreamLight/releases) [![Built on Moonlight](https://img.shields.io/badge/built%20on-Moonlight-blue?&logo=github)](https://github.com/moonlight-stream/moonlight-qt) 

**A Moonlight fork with [StreamTweak](https://github.com/FoggyBytes/StreamTweak) integration to manage host NIC speed before streaming**

> 🚨 This project is not affiliated with or endorsed by the Moonlight project.

> ⚠️ StreamLight is designed to be used exclusively in combination with [StreamTweak](https://github.com/FoggyBytes/StreamTweak) — Auto-Switch Ethernet speed for a stutter-free Moonlight ↔ Sunshine/Apollo experience, and more. Without StreamTweak running on the host PC, the StreamTweak-specific features will not function.

---

## 📖 What is StreamLight?

StreamLight is a fork of [Moonlight](https://github.com/moonlight-stream/moonlight-qt) — the open-source game streaming client — extended with native integration for [StreamTweak](https://github.com/FoggyBytes/StreamTweak), a companion tray app that automatically manages Ethernet speed on the host PC for a stutter-free streaming experience.

StreamLight is currently available for **Windows only**.

---

## ✨ Changes from upstream Moonlight

- **StreamTweak integration** — right-clicking a paired host PC now exposes two additional actions:
  - **Show host NIC speed** — queries [StreamTweak](https://github.com/FoggyBytes/StreamTweak) on the host via TCP and displays the current Ethernet adapter speed
  - **Set host to 1 Gbps** — sends a command to [StreamTweak](https://github.com/FoggyBytes/StreamTweak) to switch the host NIC to 1 Gbps before connecting; a 10-second countdown is shown before the connection is available. If no connection is made within 30 seconds, the host reverts to its original speed automatically
- **Visual theme** aligned with [StreamTweak](https://github.com/FoggyBytes/StreamTweak)'s color palette for a consistent look across both apps
- **Window title** changed to `StreamLight (a Moonlight fork)`
- **Discord link removed** — as a fork, the upstream Moonlight Discord is not the appropriate support channel
- **Auto-update checker removed** — to prevent prompts to update to upstream Moonlight releases

---

## 🖥️ Requirements

- [StreamTweak](https://github.com/FoggyBytes/StreamTweak) must be installed and running on the **host PC**
- Windows 10 or later on the **client PC**
- A Sunshine or Apollo-compatible host

---

## 📝 Installation

Download the latest installer from the [Releases](../../releases) page and run it.

---

## 🙏 Credits

StreamLight is built on top of [Moonlight](https://github.com/moonlight-stream/moonlight-qt) by the Moonlight contributors. Full credit to the original project — without their work, StreamLight would not exist.

StreamLight is released under the [GPL v3 License](LICENSE), in accordance with the upstream Moonlight license.
