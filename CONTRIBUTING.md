<!--
SPDX-FileCopyrightText: 2026 JoJo: Eyes of Heaven PC Port Contributors & shadPS4 Project
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Contributing to JoJo: Eyes of Heaven PC Port

Thank you for your interest in contributing to the **JoJo's Bizarre Adventure: Eyes of Heaven PC Port / Enhancement Project**!

This project is an open-source, community-driven effort focused on low-end hardware optimizations, Steamworks P2P online multiplayer emulation, custom launcher tooling, and compatibility enhancements.

---

## 🤖 AI Development & Contribution Policy

> [!IMPORTANT]
> **This project was engineered using Artificial Intelligence (Google DeepMind Antigravity / AI Agent systems engineering) in collaboration with human direction and testing.**

We embrace modern AI-assisted software engineering. You are **fully welcome and encouraged** to use AI tools (such as Antigravity, Claude, ChatGPT, GitHub Copilot, Cursor, etc.) when developing improvements, bug fixes, or new features for this repository, subject to the following reasonable standards:

1. **Disclosure:** Please mention if your PR or contribution was generated or assisted by AI tools in your pull request description.
2. **Quality & Verification:** All code—whether written by hand or generated with AI—must compile cleanly, run without regressions, and be tested against real game scenarios before submitting.
3. **No Blind Hallucinations:** When contributing reverse-engineered structures, NIDs, or game memory offsets, ensure they match the actual binary layout of `CUSA04909` `eboot.bin` and are validated with disassembly/decompilation evidence.

---

## 🏛️ Codebase Architecture

Key modules and their purposes:

* **`src/core/libraries/network/`**:
  * P2P networking subsystem, PlayStation Network HLE translation layer (`np_manager`, `np_matching2`, `np_signaling`, `np_web_api`, and `net`).
  * Connects game networking calls directly to Steamworks (`ISteamMatchmaking`, `ISteamNetworkingMessages`, `ISteamFriends`).
* **`src/video_core/`**:
  * Vulkan graphics backend, pipeline caching, staging buffer management, render resolution scaling, and FSR upscaling integration.
* **`src/launcher/`**:
  * Custom pre-game launcher UI built with SDL3 / ImGui (`jojo_launcher.cpp`).
  * Settings persistence (`launcher_config.toml`), keybinding remapping, Discord RPC, and Trophy viewing.
* **`src/emulator.cpp`**:
  * Core emulator orchestration and in-memory defensive assembly patches for runtime stability.

---

## 📐 Coding Standards & Style

* **Language Standard:** C++20 / C++23.
* **Formatting:**
  * 4 spaces indentation (no tabs).
  * 100 character line limit where practical.
  * Use C++ style comments (`//`).
* **Memory & Safety:**
  * Prefer modern RAII (`std::unique_ptr`, `std::shared_ptr`, `std::vector`) over raw manual memory management.
  * When communicating with PS4 guest memory, always ensure structures are placed in valid mapped guest memory (`FlexibleMemory` / `SystemManaged`) using the `PS4_SYSV_ABI` calling convention.
* **Zero Copyrighted Assets:**
  * **Never commit copyrighted game files** (`eboot.bin`, `.cpk`, `.xfbin`, copyrighted audio tracks, or commercial PS4 assets) to the repository.

---

## 🧪 Testing Your Changes

Before submitting a Pull Request:
1. Ensure the project builds cleanly without errors or warnings in **Release** mode with MSVC / Clang-cl and Ninja:
   ```powershell
   cmake --build build --config Release
   ```
2. Verify that `JoJo EoH.exe` launches the pre-game launcher correctly.
3. Test in-game booting and ensure online multiplayer hosting and lobby search function properly.

---

## 📜 Licensing

By contributing to this project, you agree that your contributions will be licensed under the **GNU General Public License v2.0 or later** (GPL-2.0-or-later).
