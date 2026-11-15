// SPDX-FileCopyrightText: Copyright 2026 JoJo EoH Standalone Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

namespace JoJoLauncher {

struct LaunchResult {
    bool should_launch_game = false;
    bool exit_requested = false;
};

// Runs the standalone JoJo's Bizarre Adventure: Eyes of Heaven Launcher GUI.
// Returns LaunchResult indicating whether the emulator core should boot into the game.
LaunchResult Run(int argc, char* argv[]);

// Returns active in-game prompt style ("PlayStation", "Xbox", "Keyboard")
std::string GetActivePromptStyle();

// Returns true if the user configured Keyboard & Mouse as active input device
bool IsPreferKeyboardMouse();

} // namespace JoJoLauncher
