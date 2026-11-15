<!--
SPDX-FileCopyrightText: 2026 JoJo: Eyes of Heaven PC Port Contributors & shadPS4 Project
SPDX-License-Identifier: GPL-2.0-or-later
-->

<div align="center">

# 🌟 JoJo's Bizarre Adventure: Eyes of Heaven — Standalone PC Port
### Low-End Hardware Optimizations • Steamworks P2P Multiplayer • Custom Launcher GUI

[![Platform](https://img.shields.io/badge/Platform-Windows%20x64-blue.svg?logo=windows)](https://github.com/Huboui/JoJo-EoH-pc-port)
[![Graphics](https://img.shields.io/badge/Vulkan-1.3+-red.svg?logo=vulkan)](https://www.vulkan.org/)
[![Multiplayer](https://img.shields.io/badge/Online-Steamworks%20P2P-black.svg?logo=steam)](https://store.steampowered.com/)
[![License](https://img.shields.io/badge/License-GPL%20v2.0-green.svg)](LICENSE)
[![Made with AI](https://img.shields.io/badge/Engineered%20With-AI%20%2F%20Antigravity-8A2BE2.svg)](#-ai-development-disclosure)

**A high-performance, standalone PC release and enhancement fork for *JoJo's Bizarre Adventure: Eyes of Heaven* (`CUSA04909`), powered by a specialized build of shadPS4.**

</div>

---

> [!NOTE]
> ### 🤖 AI Development Disclosure
> This project, including its native Steamworks P2P translation layer, Vulkan low-end performance optimizations, custom SDL3 pre-game launcher, dynamic button prompt injection, and Sony WebAPI defensive assembly hooks, was **designed and engineered using Artificial Intelligence (Google DeepMind Antigravity / AI Agent reverse-engineering & systems architecture)** in collaboration with human direction and testing.

---

## ⚡ Key Features

* **🌐 100% Pure PC Peer-to-Peer (P2P) Multiplayer:**
  * PlayStation Network server calls (`libSceNpMatching2`, `libSceNpSignaling`, `libSceNpManager`, `libSceNpWebApi`) have been completely re-architected into native Steamworks APIs (`ISteamMatchmaking`, `ISteamNetworkingMessages`, `ISteamFriends`).
  * Direct lobby creation, search, and invites directly through the **Steam Overlay (`Shift+Tab`)**—no Sony PSN servers or PS Plus subscriptions required.
  * Native host-crash prevention and memory-safe response dispatching for seamless session hosting.

* **🚀 Low-End PC Hardware Optimizations (Zero Content Cut):**
  * **Asynchronous Shader Compilation & Aggressive Pipeline Caching:** Virtually eliminates shader compilation stutters during Dual Heat Attacks (DHA) and dramatic stage transitions.
  * **Vulkan Staging Buffer Reuse & Defragmentation:** Drastically reduces VRAM spikes and overall system RAM footprint.
  * **Dynamic Render Resolution Scaler:** Native resolution scaling presets:
    * `50%` (720p Performance mode for budget GPUs / handhelds like Steam Deck)
    * `75%` (900p Balanced mode)
    * `100%` (1080p Native)
    * `133%` (1440p Quad HD)
    * `200%` (4K Ultra HD)
  * Built-in support for AMD FidelityFX Super Resolution (FSR) upscaling.

* **🎨 Custom Pre-Game Launcher GUI:**
  * Dedicated interactive configuration tool built with SDL3 / ImGui.
  * Custom Eyes of Heaven themed aesthetic with clean, responsive controls.
  * **Display Settings:** Fullscreen, Borderless Windowed, Windowed, Aspect Ratio selector (16:9, 21:9 Ultrawide, 16:10, 4:3).
  * **Keybinding Remapper:** Full interactive button mapping for both Gamepads (Xbox / PlayStation / generic XInput) and Keyboard & Mouse.
  * **Discord Rich Presence (RPC):** Displays your current stage, game mode, and elapsed time directly on Discord.
  * **Trophy Viewer:** Browse your unlocked in-game achievements and completion progress.

* **🎮 Dynamic In-Game Button Prompts:**
  * Real-time HUD prompt replacement: seamlessly displays PlayStation buttons ($\times, \bigcirc, \square, \triangle$), Xbox buttons ($A, B, X, Y$), or PC Keyboard keys depending on your selected input device.

---

## 📖 Step-by-Step Tutorial: How to Run the Mod

### 📋 Prerequisites

Before starting, ensure your system meets these basic requirements:
1. **Operating System:** Windows 10 or Windows 11 (64-bit).
2. **Graphics Card:** GPU supporting **Vulkan 1.3+** (NVIDIA GeForce GTX 900 series or newer, AMD Radeon RX 400 series or newer, Intel Iris Xe / Arc, or modern AMD Ryzen / Intel Core integrated graphics).
3. **Steam:** The **Steam client must be installed and running in the background** (logged in to any Steam account). Steam is required to power the P2P multiplayer lobbies and friend invites.
4. **Game Data:** A legally dumped copy of *JoJo's Bizarre Adventure: Eyes of Heaven* PS4 (`CUSA04909`).

---

### 📥 1. Installation

1. **Download the Release:**
   * Grab the latest standalone package (`JoJo_EoH_PC_Release_Portable.zip`) from the [Releases tab](https://github.com/Huboui/JoJo-EoH-pc-port/releases) or clone this repository.
2. **Extract the Files:**
   * Extract the ZIP contents to a directory of your choice on a fast drive (e.g., `D:\Games\JoJo EoH`).
   * *Avoid extracting directly into protected Windows directories like `C:\Program Files\` to prevent file permission issues.*

---

### 📂 2. Setting Up Game Files

Ensure your dumped game files for `CUSA04909` are placed inside the `game_data` directory alongside the executable:

```text
JoJo EoH/
├── JoJo EoH.exe                     <-- Pre-game Launcher & Game Executable
├── shadps4.exe                      <-- Direct Emulator Core
├── steam_api64.dll                  <-- Steamworks Integration Library
├── steam_appid.txt                  <-- Steam AppID Configuration (480)
├── launcher_config.toml             <-- Launcher & Graphic Settings
├── user/                            <-- Save Data, Shaders, and Logs
└── game_data/
    └── CUSA04909/
        ├── eboot.bin                <-- Game Executable
        ├── sce_sys/                 <-- Param and icon metadata
        └── ...                      <-- Game CPK and data files
```

> [!TIP]
> Make sure `steam_appid.txt` and `steam_api64.dll` remain in the same folder as `JoJo EoH.exe`.

---

### 🚀 3. Launching the Game

1. **Make Sure Steam is Running:**
   * Launch your Steam desktop client and ensure you are logged in.
2. **Start the Game:**
   * Double-click **`JoJo EoH.exe`**.
3. **The Pre-Game Launcher will open:**
   * Adjust your settings (see below).
   * Click **`Launch Game`** to start playing!

---

### ⚙️ 4. Recommended Launcher Settings

#### For Low-End PCs & Laptops:
* **Display Mode:** `Borderless Windowed` or `Exclusive Fullscreen`
* **Render Resolution:** `720p (50%)` or `900p (75%)`
* **Upscaler:** `AMD FSR`
* **Vulkan Pipeline Cache:** `Enabled`

#### For High-End PCs:
* **Display Mode:** `Exclusive Fullscreen`
* **Render Resolution:** `1080p (100%)`, `1440p (133%)`, or `4K (200%)`
* **Aspect Ratio:** `16:9` (or `21:9 Ultrawide` for ultrawide monitors)

#### Controls & Input Configuration:
* Go to the **Controls** tab in the Launcher.
* Select your preferred device: **`Gamepad`** (Xbox / DualShock / DualSense) or **`Keyboard & Mouse`**.
* Click any button to rebind it to your preferred keys or controller buttons.
* In-game button icons will automatically adapt to your chosen device.

---

### 🌐 5. How to Play Online Multiplayer

This mod replaces Sony PSN matchmaking with **Steamworks P2P**:

1. **Hosting a Match:**
   * From the in-game main menu, navigate to:  
     `Online Battle` ➔ `Player Match` ➔ `Create Session`.
   * Configure your match rules and create the room.
   * Your lobby is now hosted on your PC and broadcast via Steam.
2. **Inviting Friends:**
   * Press `Shift + Tab` to bring up the **Steam Overlay**.
   * Right-click your friend in your Steam friends list and select **`Invite to Game`**.
   * Your friend accepts the invite through Steam chat and connects directly to your session!
3. **Searching for Matches:**
   * Navigate to `Online Battle` ➔ `Player Match` ➔ `Search Session`.
   * Available public lobbies hosted by other PC players will appear in the search results.

---

## 🛠️ Troubleshooting & FAQ

<details>
<summary><b>Error: "Steam must be running in the background..."</b></summary>
<br>
Steam is required for multiplayer matchmaking and P2P communication. Start your Steam desktop application, log in, and run the game again.
</details>

<details>
<summary><b>Micro-stuttering during the first few battles</b></summary>
<br>
The first time you witness a special move (such as a Dual Heat Attack) or enter a new stage, the Vulkan driver compiles shaders. After the first time, these are permanently saved to disk in your shader cache, ensuring silky-smooth 60 FPS gameplay on subsequent runs.
</details>

<details>
<summary><b>Controller is not responding</b></summary>
<br>
Open the launcher before booting the game, select the <b>Controls</b> tab, and ensure your gamepad is selected as the active input device. If using a PlayStation controller, Steam Input or DS4Windows can be used for native XInput translation.
</details>

---

## 🔨 Building From Source

### Prerequisites
* Windows 10/11 64-bit
* [Visual Studio 2022](https://visualstudio.microsoft.com/) with C++ Desktop Development tools
* [CMake 3.28+](https://cmake.org/)
* [Ninja](https://ninja-build.org/) build system
* [Vulkan SDK](https://vulkan.lunarg.com/) 1.3+
* LLVM / Clang-cl (recommended)

### Compilation Steps
```powershell
# 1. Clone the repository with submodules
git clone --recursive https://github.com/Huboui/JoJo-EoH-pc-port.git
cd JoJo-EoH-pc-port

# 2. Configure build with CMake & Ninja
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DENABLE_CLANG=ON -DSHADPS4_ENABLE_UPDATER=OFF

# 3. Compile the release executable
cmake --build build --config Release
```

The resulting `shadps4.exe` (and launcher) will be output in the `build/` directory.

---

## 📜 Legal Disclaimer

* *JoJo's Bizarre Adventure: Eyes of Heaven* is copyright © **Bandai Namco Entertainment Inc.** and **CyberConnect2 Co., Ltd.**
* *JoJo's Bizarre Adventure* is created by **Hirohiko Araki / SHUEISHA Inc.**
* This project is an independent open-source enhancement and compatibility mod developed strictly for educational and preservation purposes.
* **No proprietary Sony PlayStation firmware files, encrypted keys, or copyrighted game assets (`.cpk`, `.xfbin`, `eboot.bin`) are distributed in this repository.** Users must dump their own legally acquired game files.

---

## 🤝 Credits & Acknowledgments

* Based on the incredible work of the [**shadPS4 Emulator Project**](https://github.com/shadps4-emu/shadPS4) and its contributors.
* **Valve Corporation** for Steamworks and the Steam networking API.
* Developed and enhanced using **Google DeepMind Antigravity** autonomous engineering systems.
