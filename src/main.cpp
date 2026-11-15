// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <vector>
#include <CLI/CLI.hpp>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_messagebox.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include "steam/steam_api.h"

#include "common/arch.h"
#include "common/key_manager.h"
#include "common/logging/log.h"
#include "common/memory_patcher.h"
#include "common/path_util.h"
#include "core/debugger.h"
#include "core/emulator_settings.h"
#include "core/emulator_state.h"
#include "core/file_sys/fs.h"
#include "core/ipc/ipc.h"
#include "core/user_settings.h"
#include "emulator.h"
#include "imgui/big_picture/big_picture.h"
#include "launcher/jojo_launcher.h"

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);

    // Auto-generate steam_appid.txt if missing
    if (!std::filesystem::exists("steam_appid.txt")) {
        std::ofstream f("steam_appid.txt");
        if (f.is_open()) {
            f << "480\n";
        }
    }

    // Mandatory Steam Check at Startup: Steam must be running
    if (!SteamAPI_IsSteamRunning() || !SteamAPI_Init()) {
        MessageBoxA(nullptr,
            "Steam must be running in the background to launch JoJo's Bizarre Adventure: Eyes of Heaven and use Online Multiplayer. Please open Steam and try again.",
            "JoJo EoH - Steam Required", MB_ICONERROR | MB_OK);
        return 1;
    }

    _putenv("SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD=1");
    _putenv("SDL_GAMECONTROLLER_IGNORE_DEVICES=");
    _putenv("SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=");
#endif
    SDL_SetHint("SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD", "1");
    SDL_SetHint("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1");
    SDL_SetHint("SDL_XINPUT_ENABLED", "1");

#if defined(__APPLE__) && defined(ARCH_X86_64)
    // KosmicKrisp only supports Apple Silicon. Check that we are not running on an Intel Mac.
    int sysctl_ret = 0;
    size_t sysctl_size = sizeof(sysctl_ret);
    sysctlbyname("sysctl.proc_translated", &sysctl_ret, &sysctl_size, nullptr, 0);
    if (sysctl_ret != 1) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "shadPS4",
                                 "shadPS4 only supports Apple Silicon Macs.", nullptr);
        std::cout << "shadPS4 only supports Apple Silicon Macs." << std::endl;
        return -1;
    }
#endif

    CLI::App app{"shadPS4 Emulator CLI"};

    // ---- CLI state ----
    std::optional<std::string> gamePath;
    std::vector<std::string> gameArgs;
    std::optional<std::filesystem::path> overrideRoot;
    std::optional<int> waitPid;
    bool waitForDebugger = false;
    bool userfaultfd = false;

    std::optional<std::string> fullscreenStr;
    bool ignoreGamePatch = false;
    bool showFps = false;
    bool configClean = false;
    bool configGlobal = false;
    bool bigPicture = false;
    bool noLauncher = false;
    bool sameProcess = false;
    bool append_log{};

    std::optional<std::filesystem::path> addGameFolder;
    std::optional<std::filesystem::path> setAddonFolder;
    std::optional<std::string> patchFile;

    std::vector<std::pair<std::filesystem::path, std::string>> mounts;
    static std::vector<std::string> env_vars;

    // ---- Options ----
    app.add_option("guest_arg", gamePath, "Game path or ID"); // positional
    app.add_option("-g,--game", gamePath, "Game path or ID");
    app.add_option("-p,--patch", patchFile, "Patch file to apply");
    app.add_flag("-i,--ignore-game-patch", ignoreGamePatch,
                 "Disable automatic loading of game patches");

    app.add_flag("-b,--big-picture", bigPicture, "Start in Big Picture Mode");
    app.add_flag("--no-launcher", noLauncher, "Skip pre-game launcher");
    app.add_flag("--direct-launch", noLauncher, "Skip pre-game launcher");
    app.add_flag("--same-process", sameProcess,
                 "Launch the game in the same process when using Big Picture Mode");

    app.add_option("-f,--fullscreen", fullscreenStr, "Fullscreen mode (true|false)");

    app.add_option("--override-root", overrideRoot)->check(CLI::ExistingDirectory);

    app.add_flag("--wait-for-debugger", waitForDebugger);
    app.add_option("--wait-for-pid", waitPid);
#ifdef __linux__
    app.add_flag("--userfaultfd", userfaultfd,
                 "Enable userfaultfd for tracking memory (Linux only)");
#endif

    app.add_flag("--show-fps", showFps);
    app.add_flag("--config-clean", configClean);
    app.add_flag("--config-global", configGlobal);
    app.add_flag("--log-append", append_log);

    app.add_option("--add-game-folder", addGameFolder)->check(CLI::ExistingDirectory);
    app.add_option("--set-addon-folder", setAddonFolder)->check(CLI::ExistingDirectory);
    app.add_option("--mount", mounts, "Mount source to destination");
    app.add_option("-e,--env", env_vars, "Environment variables to pass to the guest");

    // ---- Capture args after `--` verbatim ----
    app.allow_extras();

    // ---- No-args behavior: boot directly into game ----
    // Skip emulator UI/game list completely

    try {
        bool double_dash_found = false;
        int double_dash_index;
        for (int i = 0; i < argc; i++) {
            if (double_dash_found) {
                gameArgs.emplace_back(argv[i]);
            }
            if (!double_dash_found && std::string(argv[i]) == "--") {
                double_dash_found = true;
                double_dash_index = i;
            }
        }

        // If the -- arg is present, only parse args before it
        if (double_dash_found) {
            app.parse(double_dash_index, argv);
        } else {
            app.parse(argc, argv);
        }
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    if (waitPid)
        Core::Debugger::WaitForPid(*waitPid);

    // Initialize main log with default config
    Common::Log::Setup("shadps4.log");

    LOG_INFO(Debug, "Run: {}", fmt::join(std::span(argv, argc), " "));

    IPC::Instance().Init();

    // Initialize key manager
    KeyManager::GetInstance()->LoadFromFile();

    // Load configurations
    EmulatorSettings.Load();
    UserSettings.Load();
    // Ensure emulated network link is online when Steamworks is active
    EmulatorSettings.SetConnectedToNetwork(true);

    if (!noLauncher && !waitPid && !addGameFolder && !setAddonFolder) {
        auto launch_res = JoJoLauncher::Run(argc, argv);
        if (!launch_res.should_launch_game || launch_res.exit_requested) {
            return 0;
        }
    }

    if (bigPicture) {
        LOG_INFO(Debug, "Big picture mode bypassed: directly booting game.");
    }

    // ---- Utility commands ----
    if (addGameFolder) {
        EmulatorSettings.AddGameInstallDir(*addGameFolder);
        EmulatorSettings.Save();
        LOG_INFO(Config, "Game folder successfully saved.");
        return 0;
    }

    if (setAddonFolder) {
        EmulatorSettings.SetAddonInstallDir(*setAddonFolder);
        EmulatorSettings.Save();
        LOG_INFO(Config, "Addon folder successfully saved.");
        return 0;
    }

    const std::filesystem::path defaultGamePath = std::filesystem::current_path() / "game_data/CUSA04909/eboot.bin";
    if (!gamePath.has_value()) {
        if (!gameArgs.empty()) {
            gamePath = gameArgs.front();
            gameArgs.erase(gameArgs.begin());
        } else if (std::filesystem::exists(defaultGamePath)) {
            gamePath = defaultGamePath.string();
        } else {
            auto rel_path = std::filesystem::current_path() / "game_data/CUSA04909/eboot.bin";
            auto exe_rel_path = std::filesystem::path(argv[0]).parent_path() / "game_data/CUSA04909/eboot.bin";
            if (std::filesystem::exists(rel_path)) {
                gamePath = rel_path.string();
            } else if (std::filesystem::exists(exe_rel_path)) {
                gamePath = exe_rel_path.string();
            } else {
                gamePath = defaultGamePath.string();
            }
        }
    }

    // Auto-detect and mount extracted DLC folders from game_data/CUSA04909
    const std::filesystem::path dlcSearchDir = std::filesystem::current_path() / "game_data/CUSA04909";
    if (std::filesystem::exists(dlcSearchDir)) {
        int dlcIndex = 0;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dlcSearchDir, ec)) {
            if (!entry.is_directory()) continue;
            auto sfoPath = entry.path() / "sce_sys" / "param.sfo";
            if (std::filesystem::exists(sfoPath)) {
                std::string folderName = entry.path().filename().string();
                std::string guestMountIdx = fmt::format("/addcont{}", dlcIndex++);
                std::string guestMountName = fmt::format("/addcont/{}", folderName);
                mounts.push_back({entry.path(), guestMountIdx});
                mounts.push_back({entry.path(), guestMountName});
                LOG_INFO(Debug, "Auto-mounting DLC folder '{}' to {} and {}", folderName, guestMountIdx, guestMountName);
            }
        }
    }

    // ---- Apply flags ----
    if (patchFile)
        MemoryPatcher::patch_file = *patchFile;

    if (ignoreGamePatch)
        Core::FileSys::MntPoints::ignore_game_patches = true;

    if (fullscreenStr) {
        if (*fullscreenStr == "true") {
            EmulatorSettings.SetFullScreen(true);
        } else if (*fullscreenStr == "false") {
            EmulatorSettings.SetFullScreen(false);
        } else {
            LOG_ERROR(Debug, "Invalid argument for --fullscreen (use true|false)");
            return 1;
        }
    }

    if (showFps)
        EmulatorSettings.SetShowFpsCounter(true);

    if (configClean)
        EmulatorSettings.SetConfigMode(ConfigMode::Clean);

    if (configGlobal)
        EmulatorSettings.SetConfigMode(ConfigMode::Global);

    if (userfaultfd) {
        EmulatorSettings.SetUserfaultfdTracking(true);
    }

    // ---- Resolve game path or ID ----
    std::filesystem::path ebootPath(*gamePath);
    const auto archive_component_exists = [](const std::filesystem::path& p) -> bool {
        std::filesystem::path accum;
        for (const auto& comp : p) {
            accum /= comp;
            if (comp.extension() == ".zar") {
                return std::filesystem::is_regular_file(accum);
            }
        }
        return false;
    };
    if (!std::filesystem::exists(ebootPath) && !archive_component_exists(ebootPath)) {
        bool found = false;
        for (const auto& installDir : EmulatorSettings.GetGameInstallDirs()) {
            if (auto foundPath = Common::FS::FindGameByID(installDir, *gamePath, 0)) {
                ebootPath = *foundPath;
                found = true;
                break;
            }
        }
        if (!found) {
            for (const auto& installDir : EmulatorSettings.GetGameInstallDirs()) {
                if (auto foundPath = Common::FS::FindGameByID(installDir, *gamePath, 5)) {
                    ebootPath = *foundPath;
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            LOG_ERROR(Debug, "Game ID or file path not found: {}", *gamePath);
            return 1;
        }
    }

    auto* emulator = Common::Singleton<Core::Emulator>::Instance();
    emulator->executableName = argv[0];
    emulator->waitForDebuggerBeforeRun = waitForDebugger;
    emulator->Run(ebootPath, gameArgs, overrideRoot, mounts, env_vars, append_log);

    return 0;
}
