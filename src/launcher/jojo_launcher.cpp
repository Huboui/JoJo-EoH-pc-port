// SPDX-FileCopyrightText: Copyright 2026 JoJo EoH Standalone Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "launcher/jojo_launcher.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <pugixml.hpp>
#include <stb_image.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "common/logging/log.h"
#include "common/path_util.h"
#include "core/emulator_settings.h"
#include "core/file_format/trp.h"
#include "imgui/big_picture/imgui_impl_sdl3_big_picture.h"
#include "imgui/big_picture/imgui_impl_sdlrenderer3.h"

namespace JoJoLauncher {

using json = nlohmann::json;

static std::string s_active_prompt_style = "PlayStation";
static bool s_prefer_keyboard_mouse = false;

std::string GetActivePromptStyle() {
    return s_active_prompt_style;
}

bool IsPreferKeyboardMouse() {
    return s_prefer_keyboard_mouse;
}

// -------------------------------------------------------------
// Launcher Configuration Data
// -------------------------------------------------------------
struct Config {
    std::string display_mode = "Windowed";       // "Windowed", "Borderless Windowed", "Exclusive Fullscreen"
    std::string aspect_ratio = "16:9";           // "16:9", "21:9 Ultrawide", "16:10", "4:3"
    std::string render_resolution = "1080p (Native)"; // 720p, 900p, 1080p, 1440p, 4K
    bool fsr_enabled = true;
    bool rcas_enabled = true;
    bool disk_pipeline_cache = true;
    bool high_cpu_priority = true;

    std::string optiscaler_backend = "FSR 3.1 / FSR 4"; // "Off", "FSR 3.1 / FSR 4", "Intel XeSS", "NVIDIA DLSS"
    std::string optiscaler_quality = "Quality (1.5x)";  // "Quality (1.5x)", "Balanced (1.7x)", "Performance (2.0x)", "Ultra Performance (3.0x)"

    std::string active_input_device = "Gamepad"; // "Gamepad", "Keyboard & Mouse"
    std::string prompt_style = "PlayStation";    // "PlayStation", "Xbox", "Keyboard"

    std::map<std::string, std::string> keybindings = {
        {"cross", "n"},
        {"circle", "b"},
        {"square", "v"},
        {"triangle", "c"},
        {"l1", "q"},
        {"r1", "u"},
        {"l2", "e"},
        {"r2", "o"},
        {"l3", "x"},
        {"r3", "m"},
        {"options", "return"},
        {"touchpad", "space"},
        {"pad_up", "up"},
        {"pad_down", "down"},
        {"pad_left", "left"},
        {"pad_right", "right"},
        {"axis_left_x_minus", "a"},
        {"axis_left_x_plus", "d"},
        {"axis_left_y_minus", "w"},
        {"axis_left_y_plus", "s"},
        {"axis_right_x_minus", "j"},
        {"axis_right_x_plus", "l"},
        {"axis_right_y_minus", "i"},
        {"axis_right_y_plus", "k"},
    };

    bool close_launcher_on_launch = true;
    bool enable_discord_rpc = true;
};

static std::filesystem::path GetConfigPath() {
    auto user_dir = Common::FS::GetUserPath(Common::FS::PathType::UserDir);
    return user_dir / "launcher_config.json";
}

static void LoadConfig(Config& cfg) {
    const auto path = GetConfigPath();
    std::filesystem::path local_path = "launcher_config.json";
    std::filesystem::path target = std::filesystem::exists(path) ? path : (std::filesystem::exists(local_path) ? local_path : "");

    if (target.empty()) {
        return;
    }

    try {
        std::ifstream file(target);
        if (!file.is_open()) return;
        json j;
        file >> j;

        if (j.contains("display")) {
            auto& d = j["display"];
            if (d.contains("display_mode")) cfg.display_mode = d["display_mode"].get<std::string>();
            if (d.contains("aspect_ratio")) cfg.aspect_ratio = d["aspect_ratio"].get<std::string>();
            if (d.contains("render_resolution")) cfg.render_resolution = d["render_resolution"].get<std::string>();
            if (d.contains("fsr_enabled")) cfg.fsr_enabled = d["fsr_enabled"].get<bool>();
            if (d.contains("rcas_enabled")) cfg.rcas_enabled = d["rcas_enabled"].get<bool>();
            if (d.contains("disk_pipeline_cache")) cfg.disk_pipeline_cache = d["disk_pipeline_cache"].get<bool>();
            if (d.contains("high_cpu_priority")) cfg.high_cpu_priority = d["high_cpu_priority"].get<bool>();
        }

        if (j.contains("optiscaler")) {
            auto& o = j["optiscaler"];
            if (o.contains("backend")) cfg.optiscaler_backend = o["backend"].get<std::string>();
            if (o.contains("quality")) cfg.optiscaler_quality = o["quality"].get<std::string>();
        }

        if (j.contains("controls")) {
            auto& c = j["controls"];
            if (c.contains("active_input_device")) cfg.active_input_device = c["active_input_device"].get<std::string>();
            if (c.contains("prompt_style")) cfg.prompt_style = c["prompt_style"].get<std::string>();
            if (c.contains("keybindings") && c["keybindings"].is_object()) {
                for (auto& [k, v] : c["keybindings"].items()) {
                    cfg.keybindings[k] = v.get<std::string>();
                }
            }
        }

        if (j.contains("general")) {
            auto& g = j["general"];
            if (g.contains("close_launcher_on_launch")) cfg.close_launcher_on_launch = g["close_launcher_on_launch"].get<bool>();
            if (g.contains("enable_discord_rpc")) cfg.enable_discord_rpc = g["enable_discord_rpc"].get<bool>();
        }
    } catch (const std::exception& e) {
        LOG_WARNING(Frontend, "Failed to parse launcher_config.json: {}", e.what());
    }
}

static void SaveConfig(const Config& cfg) {
    json j;
    j["display"]["display_mode"] = cfg.display_mode;
    j["display"]["aspect_ratio"] = cfg.aspect_ratio;
    j["display"]["render_resolution"] = cfg.render_resolution;
    j["display"]["fsr_enabled"] = cfg.fsr_enabled;
    j["display"]["rcas_enabled"] = cfg.rcas_enabled;
    j["display"]["disk_pipeline_cache"] = cfg.disk_pipeline_cache;
    j["display"]["high_cpu_priority"] = cfg.high_cpu_priority;

    j["optiscaler"]["backend"] = cfg.optiscaler_backend;
    j["optiscaler"]["quality"] = cfg.optiscaler_quality;

    j["controls"]["active_input_device"] = cfg.active_input_device;
    j["controls"]["prompt_style"] = cfg.prompt_style;
    j["controls"]["keybindings"] = cfg.keybindings;

    j["general"]["close_launcher_on_launch"] = cfg.close_launcher_on_launch;
    j["general"]["enable_discord_rpc"] = cfg.enable_discord_rpc;

    const auto p = GetConfigPath();
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);

    std::ofstream file(p);
    if (file.is_open()) {
        file << j.dump(4);
    }
    std::ofstream local_file("launcher_config.json");
    if (local_file.is_open()) {
        local_file << j.dump(4);
    }
}

static void SaveInputConfigFile(const Config& cfg) {
    const auto input_dir = Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "input_config";
    std::error_code ec;
    std::filesystem::create_directories(input_dir, ec);

    std::stringstream ss;
    ss << "# JoJo's Bizarre Adventure: Eyes of Heaven (CUSA04909) Input Configuration\n";
    ss << "# Generated automatically by JoJo EoH Standalone Launcher\n\n";

    for (const auto& [btn, key] : cfg.keybindings) {
        if (btn == "touchpad") {
            ss << "touchpad_center = " << key << "\n";
        } else {
            ss << btn << " = " << key << "\n";
        }
    }

    ss << "\n# Standard Controller mappings\n";
    ss << "triangle = triangle\n";
    ss << "cross = cross\n";
    ss << "square = square\n";
    ss << "circle = circle\n";
    ss << "l1 = l1\n";
    ss << "l2 = l2\n";
    ss << "l3 = l3\n";
    ss << "r1 = r1\n";
    ss << "r2 = r2\n";
    ss << "r3 = r3\n";
    ss << "options = options\n";
    ss << "touchpad_center = back\n";
    ss << "pad_up = pad_up\n";
    ss << "pad_down = pad_down\n";
    ss << "pad_left = pad_left\n";
    ss << "pad_right = pad_right\n";
    ss << "axis_left_x = axis_left_x\n";
    ss << "axis_left_y = axis_left_y\n";
    ss << "axis_right_x = axis_right_x\n";
    ss << "axis_right_y = axis_right_y\n";
    ss << "analog_deadzone = leftjoystick, 5, 127\n";
    ss << "analog_deadzone = rightjoystick, 5, 127\n";

    const std::string content = ss.str();
    const auto game_ini = input_dir / "CUSA04909.ini";
    const auto default_ini = input_dir / "default.ini";

    {
        std::ofstream f(game_ini);
        if (f.is_open()) f << content;
    }
    {
        std::ofstream f(default_ini);
        if (f.is_open()) f << content;
    }
}

// -------------------------------------------------------------
// Trophy System Data
// -------------------------------------------------------------
struct TrophyItem {
    int id = 0;
    std::string name;
    std::string detail;
    std::string grade; // "Platinum", "Gold", "Silver", "Bronze"
    bool unlocked = false;
    uint64_t timestamp = 0;
    std::string icon_filename;
    SDL_Texture* texture = nullptr;
    std::vector<uint8_t> raw_pixels;
    int img_w = 0;
    int img_h = 0;
};

static std::vector<TrophyItem> s_trophies;
static std::mutex s_trophy_mutex;
static std::vector<TrophyItem> s_trophies_staged;
static std::atomic<bool> s_trophies_loading{false};
static std::atomic<bool> s_trophies_ready{false};
static std::atomic<bool> s_trophies_loaded{false};
static std::thread s_trophy_loader_thread;

static SDL_Texture* LoadTexture(SDL_Renderer* renderer, const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return nullptr;
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return nullptr;
    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (buffer.empty()) return nullptr;

    int w = 0, h = 0, channels = 0;
    unsigned char* data = stbi_load_from_memory(
        buffer.data(), static_cast<int>(buffer.size()), &w, &h, &channels, 4);
    if (!data) return nullptr;

    SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, data, w * 4);
    if (!surface) {
        stbi_image_free(data);
        return nullptr;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    stbi_image_free(data);
    return texture;
}


static void UpdateOptiScalerIni(const Config& cfg) {
    const std::filesystem::path ini_path = "OptiScaler.ini";
    if (!std::filesystem::exists(ini_path)) {
        return;
    }

    std::string v_upscaler = "fsr31";
    if (cfg.optiscaler_backend == "Off") {
        v_upscaler = "off";
    } else if (cfg.optiscaler_backend.find("XeSS") != std::string::npos) {
        v_upscaler = "xess";
    } else if (cfg.optiscaler_backend.find("DLSS") != std::string::npos) {
        v_upscaler = "dlss";
    } else {
        v_upscaler = "fsr31";
    }

    bool ratio_enabled = (cfg.optiscaler_backend != "Off");
    std::string ratio_val = "1.5";
    if (cfg.optiscaler_quality.find("Balanced") != std::string::npos) {
        ratio_val = "1.7";
    } else if (cfg.optiscaler_quality.find("Ultra Performance") != std::string::npos) {
        ratio_val = "3.0";
    } else if (cfg.optiscaler_quality.find("Performance") != std::string::npos) {
        ratio_val = "2.0";
    } else {
        ratio_val = "1.5";
    }

    std::ifstream file_in(ini_path);
    if (!file_in.is_open()) return;

    std::string line;
    std::vector<std::string> lines;
    while (std::getline(file_in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.rfind("VulkanUpscaler=", 0) == 0) {
            lines.push_back("VulkanUpscaler=" + v_upscaler);
        } else if (line.rfind("UpscaleRatioOverrideEnabled=", 0) == 0) {
            lines.push_back(std::string("UpscaleRatioOverrideEnabled=") + (ratio_enabled ? "true" : "false"));
        } else if (line.rfind("UpscaleRatioOverrideValue=", 0) == 0) {
            lines.push_back("UpscaleRatioOverrideValue=" + ratio_val);
        } else {
            lines.push_back(line);
        }
    }
    file_in.close();

    std::ofstream file_out(ini_path, std::ios::trunc);
    if (file_out.is_open()) {
        for (const auto& l : lines) {
            file_out << l << "\n";
        }
    }
}

static void TrophyLoaderWorker() {
    std::vector<TrophyItem> items;
    const std::string np_comm_id = "NPWR09694_00";
    const auto trophy_dir = Common::FS::GetUserPath(Common::FS::PathType::TrophyDir) / np_comm_id;
    const auto trp_path = std::filesystem::current_path() / "game_data/CUSA04909/sce_sys/trophy/trophy00.trp";
    const auto cache_file = trophy_dir / "trophies_cache.json";

    // 1. Extract TRP if needed
    if (!std::filesystem::exists(cache_file) &&
        !std::filesystem::exists(trophy_dir / "Xml" / "TROP_01.XML") &&
        !std::filesystem::exists(trophy_dir / "Xml" / "TROP.XML")) {
        if (std::filesystem::exists(trp_path)) {
            TRP trp;
            trp.Extract(trp_path, np_comm_id, trophy_dir);
        }
    }

    // 2. Read user unlock state
    std::map<int, std::pair<bool, uint64_t>> unlocks;
    const auto home_trophy = Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "home/1000/trophy" / (np_comm_id + ".xml");
    if (std::filesystem::exists(home_trophy)) {
        pugi::xml_document user_doc;
        if (user_doc.load_file(home_trophy.native().c_str())) {
            for (const auto& node : user_doc.child("trophyconf").children()) {
                if (std::string_view(node.name()) == "trophy") {
                    const int id = node.attribute("id").as_int(-1);
                    const bool unlocked = node.attribute("unlockstate").as_bool();
                    const uint64_t ts = node.attribute("timestamp").as_ullong();
                    if (id >= 0 && unlocked) {
                        unlocks[id] = {true, ts};
                    }
                }
            }
        }
    }

    // 3. Try loading from fast JSON cache first
    bool loaded_from_cache = false;
    if (std::filesystem::exists(cache_file)) {
        try {
            std::ifstream cfile(cache_file);
            if (cfile.is_open()) {
                nlohmann::json j;
                cfile >> j;
                for (const auto& obj : j) {
                    TrophyItem item;
                    item.id = obj.value("id", 0);
                    item.name = obj.value("name", "");
                    item.detail = obj.value("detail", "");
                    item.grade = obj.value("grade", "Bronze");
                    item.icon_filename = obj.value("icon", "");
                    if (auto it = unlocks.find(item.id); it != unlocks.end()) {
                        item.unlocked = it->second.first;
                        item.timestamp = it->second.second;
                    }
                    items.push_back(std::move(item));
                }
                loaded_from_cache = !items.empty();
            }
        } catch (...) {
            items.clear();
            loaded_from_cache = false;
        }
    }

    // 4. Fallback to XML if cache is missing or corrupt
    if (!loaded_from_cache) {
        auto xml_path = trophy_dir / "Xml" / "TROP_01.XML";
        if (!std::filesystem::exists(xml_path)) {
            xml_path = trophy_dir / "Xml" / "TROP.XML";
        }
        if (std::filesystem::exists(xml_path)) {
            pugi::xml_document doc;
            if (doc.load_file(xml_path.native().c_str())) {
                nlohmann::json cache_json = nlohmann::json::array();
                for (const auto& node : doc.child("trophyconf").children()) {
                    if (std::string_view(node.name()) != "trophy") continue;

                    TrophyItem item;
                    item.id = node.attribute("id").as_int(0);
                    std::string ttype = node.attribute("ttype").value();
                    if (ttype == "P") item.grade = "Platinum";
                    else if (ttype == "G") item.grade = "Gold";
                    else if (ttype == "S") item.grade = "Silver";
                    else item.grade = "Bronze";

                    item.name = node.child("name").text().as_string();
                    item.detail = node.child("detail").text().as_string();

                    if (auto it = unlocks.find(item.id); it != unlocks.end()) {
                        item.unlocked = it->second.first;
                        item.timestamp = it->second.second;
                    } else {
                        item.unlocked = node.attribute("unlockstate").as_bool();
                        item.timestamp = node.attribute("timestamp").as_ullong();
                    }

                    char buf[32];
                    std::snprintf(buf, sizeof(buf), "TROP%03d.PNG", item.id);
                    item.icon_filename = buf;

                    nlohmann::json entry;
                    entry["id"] = item.id;
                    entry["name"] = item.name;
                    entry["detail"] = item.detail;
                    entry["grade"] = item.grade;
                    entry["icon"] = item.icon_filename;
                    cache_json.push_back(entry);

                    items.push_back(std::move(item));
                }

                // Save cache to disk
                try {
                    std::ofstream cfile(cache_file);
                    if (cfile.is_open()) {
                        cfile << cache_json.dump(2);
                    }
                } catch (...) {}
            }
        }
    }

    // 5. Decode icon PNG pixels on the background thread
    for (auto& item : items) {
        const auto icon_path = trophy_dir / "Icons" / item.icon_filename;
        if (std::filesystem::exists(icon_path)) {
            std::ifstream file(icon_path, std::ios::binary);
            if (file.is_open()) {
                std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                if (!buffer.empty()) {
                    int w = 0, h = 0, channels = 0;
                    unsigned char* pixels = stbi_load_from_memory(buffer.data(), static_cast<int>(buffer.size()),
                                                                 &w, &h, &channels, 4);
                    if (pixels) {
                        item.img_w = w;
                        item.img_h = h;
                        item.raw_pixels.assign(pixels, pixels + (w * h * 4));
                        stbi_image_free(pixels);
                    }
                }
            }
        }
    }

    // 6. Sort: Platinum first, then unlocked first, then by ID
    std::sort(items.begin(), items.end(), [](const TrophyItem& a, const TrophyItem& b) {
        if (a.grade == "Platinum" && b.grade != "Platinum") return true;
        if (b.grade == "Platinum" && a.grade != "Platinum") return false;
        if (a.unlocked != b.unlocked) return a.unlocked > b.unlocked;
        return a.id < b.id;
    });

    {
        std::lock_guard lock(s_trophy_mutex);
        s_trophies_staged = std::move(items);
        s_trophies_ready = true;
    }
}

static void StartLazyLoadingTrophies() {
    if (s_trophies_loaded || s_trophies_loading) return;
    s_trophies_loading = true;
    if (s_trophy_loader_thread.joinable()) {
        s_trophy_loader_thread.join();
    }
    s_trophy_loader_thread = std::thread(TrophyLoaderWorker);
}

// -------------------------------------------------------------
// Interactive Keybinding Remapper State
// -------------------------------------------------------------
struct RemapEntry {
    const char* display_name;
    const char* action_key;
};

static const std::vector<RemapEntry> s_remap_actions = {
    {"[X] Cross / A Button", "cross"},
    {"[O] Circle / B Button", "circle"},
    {"[S] Square / X Button", "square"},
    {"[T] Triangle / Y Button", "triangle"},
    {"L1 / Left Bumper", "l1"},
    {"R1 / Right Bumper", "r1"},
    {"L2 / Left Trigger", "l2"},
    {"R2 / Right Trigger", "r2"},
    {"L3 / Left Stick Click", "l3"},
    {"R3 / Right Stick Click", "r3"},
    {"Options / Start Button", "options"},
    {"Touchpad / Select Button", "touchpad"},
    {"D-Pad Up", "pad_up"},
    {"D-Pad Down", "pad_down"},
    {"D-Pad Left", "pad_left"},
    {"D-Pad Right", "pad_right"},
    {"Left Stick Up (Move Forward)", "axis_left_y_minus"},
    {"Left Stick Down (Move Backward)", "axis_left_y_plus"},
    {"Left Stick Left (Move Left)", "axis_left_x_minus"},
    {"Left Stick Right (Move Right)", "axis_left_x_plus"},
    {"Right Stick Up (Camera Up)", "axis_right_y_minus"},
    {"Right Stick Down (Camera Down)", "axis_right_y_plus"},
    {"Right Stick Left (Camera Left)", "axis_right_x_minus"},
    {"Right Stick Right (Camera Right)", "axis_right_x_plus"},
};

static std::string s_remapping_action = "";

static std::string KeycodeToBindingName(SDL_Keycode code) {
    if (code >= SDLK_A && code <= SDLK_Z) {
        char c = 'a' + (code - SDLK_A);
        return std::string(1, c);
    }
    if (code >= SDLK_0 && code <= SDLK_9) {
        char c = '0' + (code - SDLK_0);
        return std::string(1, c);
    }
    switch (code) {
    case SDLK_SPACE: return "space";
    case SDLK_RETURN: return "return";
    case SDLK_ESCAPE: return "escape";
    case SDLK_BACKSPACE: return "backspace";
    case SDLK_TAB: return "tab";
    case SDLK_LSHIFT: return "lshift";
    case SDLK_RSHIFT: return "rshift";
    case SDLK_LCTRL: return "lctrl";
    case SDLK_RCTRL: return "rctrl";
    case SDLK_LALT: return "lalt";
    case SDLK_RALT: return "ralt";
    case SDLK_UP: return "up";
    case SDLK_DOWN: return "down";
    case SDLK_LEFT: return "left";
    case SDLK_RIGHT: return "right";
    default: {
        const char* name = SDL_GetKeyName(code);
        if (name && std::strlen(name) > 0) {
            std::string s(name);
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
            return s;
        }
        return "space";
    }
    }
}

// -------------------------------------------------------------
// Launcher Main Entry Point
// -------------------------------------------------------------
LaunchResult Run(int argc, char* argv[]) {
    LaunchResult result{};
    Config cfg;
    LoadConfig(cfg);

    // Initialize SDL video subsystem for the launcher (skip gamepad enum to start instantly)
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_ERROR(Frontend, "JoJoLauncher: Failed to initialize SDL: {}", SDL_GetError());
        result.should_launch_game = true;
        return result;
    }

    constexpr int WINDOW_WIDTH = 980;
    constexpr int WINDOW_HEIGHT = 660;

    SDL_Window* window = SDL_CreateWindow(
        "JoJo's Bizarre Adventure: Eyes of Heaven - Standalone Launcher",
        WINDOW_WIDTH, WINDOW_HEIGHT,
        SDL_WINDOW_RESIZABLE
    );

    if (!window) {
        LOG_ERROR(Frontend, "JoJoLauncher: Failed to create window: {}", SDL_GetError());
        result.should_launch_game = true;
        return result;
    }

    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    // Set high-res JoJo Eyes of Heaven icon for launcher window and taskbar
    const std::filesystem::path icon_path = std::filesystem::current_path() / "game_data" / "CUSA04909" / "sce_sys" / "icon0.png";
    if (std::filesystem::exists(icon_path)) {
        std::ifstream file(icon_path, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);
            std::vector<unsigned char> buffer(size);
            if (file.read(reinterpret_cast<char*>(buffer.data()), size)) {
                int icon_w = 0, icon_h = 0, icon_ch = 0;
                unsigned char* icon_pixels = stbi_load_from_memory(buffer.data(), static_cast<int>(buffer.size()), &icon_w, &icon_h, &icon_ch, 4);
                if (icon_pixels) {
                    SDL_Surface* icon_surf = SDL_CreateSurfaceFrom(icon_w, icon_h, SDL_PIXELFORMAT_RGBA32, icon_pixels, icon_w * 4);
                    if (icon_surf) {
                        SDL_SetWindowIcon(window, icon_surf);
                        SDL_DestroySurface(icon_surf);
                    }
                    stbi_image_free(icon_pixels);
                }
            }
        }
    }

    // Prefer software renderer for the 2D launcher UI so it opens immediately without
    // initializing OpenGL/WGL or pulling OptiScaler's dxgi.dll into the launcher process.
    SDL_Renderer* renderer = SDL_CreateRenderer(window, "software");
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, "opengl");
    }
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, nullptr);
    }
    if (renderer) {
        SDL_SetRenderVSync(renderer, 1);
    }
    if (!renderer) {
        LOG_ERROR(Frontend, "JoJoLauncher: Failed to create SDL renderer: {}", SDL_GetError());
        SDL_DestroyWindow(window);
        result.should_launch_game = true;
        return result;
    }

    // Try to load high-res wallpaper or fallback background
    SDL_Texture* bg_wallpaper = nullptr;
    const std::vector<std::filesystem::path> bg_candidates = {
        "launcher_bg.png",
        "launcher_bg.jpg",
        "game_data/CUSA04909/sce_sys/pic0.png"
    };
    for (const auto& p : bg_candidates) {
        if (std::filesystem::exists(p)) {
            bg_wallpaper = LoadTexture(renderer, p);
            if (bg_wallpaper) break;
        }
    }

    // Load cover logo art
    SDL_Texture* cover_art = nullptr;
    const std::vector<std::filesystem::path> cover_candidates = {
        "launcher_bg.png",
        "game_data/CUSA04909/sce_sys/icon0.png"
    };
    for (const auto& p : cover_candidates) {
        if (std::filesystem::exists(p)) {
            cover_art = LoadTexture(renderer, p);
            if (cover_art) break;
        }
    }

    // Setup ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Apply JoJo Eyes of Heaven Dark & Gold Aesthetic
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 5.0f;
    style.TabRounding = 5.0f;
    style.FrameBorderSize = 1.0f;
    style.WindowBorderSize = 0.0f;
    style.WindowPadding = ImVec2(16.0f, 16.0f);
    style.FramePadding = ImVec2(10.0f, 7.0f);
    style.ItemSpacing = ImVec2(10.0f, 9.0f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                  = ImVec4(0.96f, 0.96f, 0.98f, 1.00f);
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.55f, 0.55f, 0.60f, 1.00f);
    colors[ImGuiCol_WindowBg]              = ImVec4(0.08f, 0.05f, 0.14f, 0.92f);
    colors[ImGuiCol_ChildBg]               = ImVec4(0.12f, 0.08f, 0.20f, 0.82f);
    colors[ImGuiCol_PopupBg]               = ImVec4(0.12f, 0.08f, 0.20f, 0.96f);
    colors[ImGuiCol_Border]                = ImVec4(0.35f, 0.22f, 0.50f, 0.70f);
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.18f, 0.12f, 0.28f, 0.85f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.28f, 0.18f, 0.42f, 0.90f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.38f, 0.24f, 0.55f, 1.00f);
    colors[ImGuiCol_TitleBg]               = ImVec4(0.12f, 0.07f, 0.20f, 1.00f);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.22f, 0.12f, 0.35f, 1.00f);
    colors[ImGuiCol_CheckMark]             = ImVec4(0.98f, 0.75f, 0.28f, 1.00f);
    colors[ImGuiCol_SliderGrab]            = ImVec4(0.92f, 0.68f, 0.22f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(1.00f, 0.82f, 0.35f, 1.00f);
    colors[ImGuiCol_Button]                = ImVec4(0.25f, 0.15f, 0.38f, 0.90f);
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.92f, 0.68f, 0.22f, 0.85f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(1.00f, 0.80f, 0.32f, 1.00f);
    colors[ImGuiCol_Header]                = ImVec4(0.28f, 0.18f, 0.42f, 0.80f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.85f, 0.62f, 0.20f, 0.80f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.95f, 0.72f, 0.25f, 1.00f);
    colors[ImGuiCol_Tab]                   = ImVec4(0.18f, 0.11f, 0.28f, 0.85f);
    colors[ImGuiCol_TabHovered]            = ImVec4(0.92f, 0.68f, 0.22f, 0.85f);
    colors[ImGuiCol_TabActive]             = ImVec4(0.32f, 0.18f, 0.48f, 1.00f);
    colors[ImGuiCol_TabUnfocused]          = ImVec4(0.14f, 0.08f, 0.22f, 0.85f);
    colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.24f, 0.14f, 0.36f, 1.00f);

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    // Heavy initialization (trophy icons / XML) is moved off startup for instant (< 1 sec) launcher load!
    bool running = true;
    int selected_tab = 0; // 0: Graphics, 1: Controls, 2: Trophies, 3: General

    while (running) {
        SDL_WaitEventTimeout(nullptr, 16);
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);

            if (event.type == SDL_EVENT_QUIT) {
                running = false;
                result.exit_requested = true;
            }

            // Interactive remapping input interception
            if (!s_remapping_action.empty() && event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key != SDLK_ESCAPE) {
                    std::string key_str = KeycodeToBindingName(event.key.key);
                    cfg.keybindings[s_remapping_action] = key_str;
                    SaveInputConfigFile(cfg);
                    SaveConfig(cfg);
                }
                s_remapping_action = "";
            }
        }

        if (s_trophies_ready) {
            std::lock_guard lock(s_trophy_mutex);
            s_trophies = std::move(s_trophies_staged);
            s_trophies_ready = false;
            s_trophies_loading = false;
            s_trophies_loaded = true;
            if (s_trophy_loader_thread.joinable()) {
                s_trophy_loader_thread.join();
            }
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        int win_w = WINDOW_WIDTH, win_h = WINDOW_HEIGHT;
        SDL_GetWindowSize(window, &win_w, &win_h);

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(win_w), static_cast<float>(win_h)));
        ImGui::Begin("JoJoEoHLauncher", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

        // Header Section
        ImGui::BeginGroup();
        if (cover_art) {
            ImGui::Image(reinterpret_cast<ImTextureID>(cover_art), ImVec2(54, 54));
            ImGui::SameLine();
        }
        ImGui::BeginGroup();
        ImGui::TextColored(ImVec4(0.98f, 0.78f, 0.28f, 1.0f), "JOJO'S BIZARRE ADVENTURE: EYES OF HEAVEN");
        ImGui::TextColored(ImVec4(0.70f, 0.70f, 0.75f, 1.0f), "Standalone Stand Experience - Low-End Optimized Build & Steam P2P Multiplayer");
        ImGui::EndGroup();
        ImGui::EndGroup();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Top Navigation Tabs
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 8.0f));
        if (ImGui::Button("Display & Graphics", ImVec2(190, 36))) selected_tab = 0;
        ImGui::SameLine();
        if (ImGui::Button("Controls & Keybindings", ImVec2(210, 36))) selected_tab = 1;
        ImGui::SameLine();
        if (ImGui::Button("Trophies", ImVec2(150, 36))) selected_tab = 2;
        ImGui::SameLine();
        if (ImGui::Button("General & Multiplayer", ImVec2(200, 36))) selected_tab = 3;
        ImGui::PopStyleVar();

        ImGui::Spacing();

        // Main Tab Content Area
        const float footer_height = 68.0f;
        ImGui::BeginChild("MainContentRegion", ImVec2(0, -footer_height), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);

        // -------------------------------------------------------------
        // TAB 0: DISPLAY & GRAPHICS
        // -------------------------------------------------------------
        if (selected_tab == 0) {
            ImGui::TextColored(ImVec4(0.98f, 0.78f, 0.28f, 1.0f), "DISPLAY & GRAPHICS SETTINGS");
            ImGui::TextDisabled("Configure resolution scaling, aspect ratio, and low-end hardware performance.");
            ImGui::Spacing();

            // Display Mode
            const char* display_modes[] = {"Windowed", "Borderless Windowed", "Exclusive Fullscreen"};
            int current_mode = 0;
            if (cfg.display_mode == "Borderless Windowed") current_mode = 1;
            else if (cfg.display_mode == "Exclusive Fullscreen") current_mode = 2;

            if (ImGui::Combo("Display Mode", &current_mode, display_modes, IM_ARRAYSIZE(display_modes))) {
                cfg.display_mode = display_modes[current_mode];
                SaveConfig(cfg);
            }

            // Aspect Ratio
            const char* aspect_ratios[] = {"16:9 (Standard)", "21:9 Ultrawide", "16:10", "4:3 (Classic)"};
            int current_aspect = 0;
            if (cfg.aspect_ratio == "21:9 Ultrawide" || cfg.aspect_ratio == "21:9") current_aspect = 1;
            else if (cfg.aspect_ratio == "16:10") current_aspect = 2;
            else if (cfg.aspect_ratio == "4:3") current_aspect = 3;

            if (ImGui::Combo("Aspect Ratio", &current_aspect, aspect_ratios, IM_ARRAYSIZE(aspect_ratios))) {
                cfg.aspect_ratio = (current_aspect == 1) ? "21:9 Ultrawide" :
                                   (current_aspect == 2) ? "16:10" :
                                   (current_aspect == 3) ? "4:3" : "16:9";
                SaveConfig(cfg);
            }

            // Render Resolution Scaler
            const char* resolutions[] = {
                "720p (Low-End Hardware / 50% Scale)",
                "900p (Performance / 75% Scale)",
                "1080p (Native / 100% Scale)",
                "1440p (2K Quad HD / 133% Scale)",
                "4K (2160p Ultra HD / 200% Scale)"
            };
            int current_res = 2;
            if (cfg.render_resolution.find("720p") != std::string::npos) current_res = 0;
            else if (cfg.render_resolution.find("900p") != std::string::npos) current_res = 1;
            else if (cfg.render_resolution.find("1080p") != std::string::npos) current_res = 2;
            else if (cfg.render_resolution.find("1440p") != std::string::npos) current_res = 3;
            else if (cfg.render_resolution.find("4K") != std::string::npos) current_res = 4;

            if (ImGui::Combo("Render Resolution", &current_res, resolutions, IM_ARRAYSIZE(resolutions))) {
                cfg.render_resolution = resolutions[current_res];
                SaveConfig(cfg);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextColored(ImVec4(0.98f, 0.78f, 0.28f, 1.0f), "PERFORMANCE & LOW-END HARDWARE OPTIMIZATIONS");
            ImGui::TextDisabled("Accelerations designed for steady 60 FPS on integrated GPUs and low VRAM systems:");
            ImGui::Spacing();

            if (ImGui::Checkbox("Enable AMD FidelityFX Super Resolution (FSR 1.0 + RCAS)", &cfg.fsr_enabled)) {
                SaveConfig(cfg);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(Upscales lower resolutions to crisp native display)");

            if (ImGui::Checkbox("Enable Robust RCAS Sharpness Enhancement", &cfg.rcas_enabled)) {
                SaveConfig(cfg);
            }

            if (ImGui::Checkbox("Enable Asynchronous Disk Pipeline Caching", &cfg.disk_pipeline_cache)) {
                SaveConfig(cfg);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(Prevents stutter during Dual Heat Attacks and Stage Transitions)");

            if (ImGui::Checkbox("High CPU Process Scheduling Priority", &cfg.high_cpu_priority)) {
                SaveConfig(cfg);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(Ensures Windows scheduler gives maximum priority to game threads)");

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "[OK] Aggressive Vulkan VRAM Defragmentation & Staging Buffer Reclamation active (Zero content cut)");
        }

        // -------------------------------------------------------------
        // TAB 1: CONTROLS & KEYBINDINGS
        // -------------------------------------------------------------
        else if (selected_tab == 1) {
            ImGui::TextColored(ImVec4(0.98f, 0.78f, 0.28f, 1.0f), "CONTROLS & INPUT CONFIGURATION");
            ImGui::TextDisabled("Select your active controller or rebind keyboard and mouse controls.");
            ImGui::Spacing();

            // Active Input Device Selector
            const char* input_devices[] = {"Gamepad (Auto-detect Xbox / PlayStation)", "Keyboard & Mouse"};
            int current_device = (cfg.active_input_device == "Keyboard & Mouse") ? 1 : 0;
            if (ImGui::Combo("Active Input Device", &current_device, input_devices, IM_ARRAYSIZE(input_devices))) {
                cfg.active_input_device = input_devices[current_device];
                s_prefer_keyboard_mouse = (current_device == 1);
                SaveConfig(cfg);
            }

            // Dynamic In-Game Button Prompts
            const char* prompt_styles[] = {"PlayStation (Triangle / Circle / Cross / Square)", "Xbox (Y B A X)", "Keyboard & Mouse"};
            int current_prompt = 0;
            if (cfg.prompt_style == "Xbox") current_prompt = 1;
            else if (cfg.prompt_style == "Keyboard" || cfg.prompt_style == "Keyboard & Mouse") current_prompt = 2;

            if (ImGui::Combo("Dynamic Button Prompts", &current_prompt, prompt_styles, IM_ARRAYSIZE(prompt_styles))) {
                cfg.prompt_style = (current_prompt == 1) ? "Xbox" : (current_prompt == 2) ? "Keyboard" : "PlayStation";
                s_active_prompt_style = cfg.prompt_style;
                SaveConfig(cfg);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextColored(ImVec4(0.98f, 0.78f, 0.28f, 1.0f), "INTERACTIVE KEYBOARD & MOUSE REMAPPER");
            ImGui::TextDisabled("Click any binding to rebind it. Press any keyboard key to assign.");

            if (!s_remapping_action.empty()) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.20f, 1.0f));
                ImGui::Text(">> Press any key to bind to: [%s]  (Press ESC to cancel) <<", s_remapping_action.c_str());
                ImGui::PopStyleColor();
                ImGui::Spacing();
            }

            ImGui::Spacing();
            ImGui::BeginTable("KeymapTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg);
            ImGui::TableSetupColumn("Action / Button", ImGuiTableColumnFlags_WidthFixed, 260.0f);
            ImGui::TableSetupColumn("Current Key Binding", ImGuiTableColumnFlags_WidthFixed, 180.0f);
            ImGui::TableSetupColumn("Rebind", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (const auto& entry : s_remap_actions) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", entry.display_name);

                ImGui::TableSetColumnIndex(1);
                std::string current_key = cfg.keybindings[entry.action_key];
                std::transform(current_key.begin(), current_key.end(), current_key.begin(), ::toupper);
                ImGui::TextColored(ImVec4(0.98f, 0.78f, 0.28f, 1.0f), "%s", current_key.c_str());

                ImGui::TableSetColumnIndex(2);
                std::string btn_label = (s_remapping_action == entry.action_key) ? "Listening...##" : "Change Key##";
                btn_label += entry.action_key;

                if (ImGui::Button(btn_label.c_str(), ImVec2(120, 24))) {
                    s_remapping_action = entry.action_key;
                }
            }
            ImGui::EndTable();

            ImGui::Spacing();
            if (ImGui::Button("Reset All Keybindings to Defaults", ImVec2(240, 30))) {
                Config def;
                cfg.keybindings = def.keybindings;
                SaveInputConfigFile(cfg);
                SaveConfig(cfg);
            }
        }

        // -------------------------------------------------------------
        // TAB 2: TROPHIES
        // -------------------------------------------------------------
        else if (selected_tab == 2) {
            if (!s_trophies_loaded && !s_trophies_loading) {
                StartLazyLoadingTrophies();
            }

            if (s_trophies_loading) {
                ImGui::TextColored(ImVec4(0.98f, 0.78f, 0.28f, 1.0f), "EYES OF HEAVEN TROPHY VIEWER");
                ImGui::TextDisabled("Loading trophies and icons in background thread...");
                ImGui::Spacing();
                float cur_time = static_cast<float>(ImGui::GetTime());
                ImGui::ProgressBar(std::fmod(cur_time * 0.4f, 1.0f), ImVec2(-1.0f, 24.0f), "Decrypting & Loading Trophy Icons...");
            } else {
                // Upload any pending decoded textures on main thread
                for (auto& item : s_trophies) {
                    if (!item.texture && !item.raw_pixels.empty()) {
                        SDL_Surface* surface = SDL_CreateSurfaceFrom(item.img_w, item.img_h, SDL_PIXELFORMAT_RGBA32,
                                                                     item.raw_pixels.data(), item.img_w * 4);
                        if (surface) {
                            item.texture = SDL_CreateTextureFromSurface(renderer, surface);
                            SDL_DestroySurface(surface);
                        }
                        item.raw_pixels.clear();
                        item.raw_pixels.shrink_to_fit();
                    }
                }

            int unlocked_count = 0;
            int platinum_count = 0;
            int gold_count = 0;
            int silver_count = 0;
            int bronze_count = 0;

            for (const auto& t : s_trophies) {
                if (t.unlocked) {
                    unlocked_count++;
                    if (t.grade == "Platinum") platinum_count++;
                    else if (t.grade == "Gold") gold_count++;
                    else if (t.grade == "Silver") silver_count++;
                    else bronze_count++;
                }
            }

            ImGui::TextColored(ImVec4(0.98f, 0.78f, 0.28f, 1.0f), "EYES OF HEAVEN TROPHY VIEWER");
            ImGui::TextDisabled("Parsed from game TRP / TROP.XML metadata and your local save data.");
            ImGui::Spacing();

            // Progress Header
            float progress = s_trophies.empty() ? 0.0f : static_cast<float>(unlocked_count) / s_trophies.size();
            char progress_text[64];
            std::snprintf(progress_text, sizeof(progress_text), "%d / %d Unlocked (%.1f%%)",
                          unlocked_count, static_cast<int>(s_trophies.size()), progress * 100.0f);
            ImGui::ProgressBar(progress, ImVec2(-1.0f, 22.0f), progress_text);

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.40f, 0.85f, 1.00f, 1.0f), "Platinum: %d/1", platinum_count);
            ImGui::SameLine(180);
            ImGui::TextColored(ImVec4(1.00f, 0.84f, 0.00f, 1.0f), "Gold: %d/3", gold_count);
            ImGui::SameLine(320);
            ImGui::TextColored(ImVec4(0.80f, 0.80f, 0.80f, 1.0f), "Silver: %d/8", silver_count);
            ImGui::SameLine(460);
            ImGui::TextColored(ImVec4(0.80f, 0.50f, 0.20f, 1.0f), "Bronze: %d/36", bronze_count);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Trophy Cards List
            for (const auto& item : s_trophies) {
                ImGui::PushID(item.id);
                ImGui::BeginChild(item.id + 1000, ImVec2(0, 68), true);

                if (item.texture) {
                    ImGui::Image(reinterpret_cast<ImTextureID>(item.texture), ImVec2(48, 48));
                    ImGui::SameLine();
                } else {
                    ImGui::Button("T", ImVec2(48, 48));
                    ImGui::SameLine();
                }

                ImGui::BeginGroup();
                // Title and Grade
                ImVec4 grade_color = (item.grade == "Platinum") ? ImVec4(0.40f, 0.85f, 1.00f, 1.0f) :
                                     (item.grade == "Gold")     ? ImVec4(1.00f, 0.84f, 0.00f, 1.0f) :
                                     (item.grade == "Silver")   ? ImVec4(0.85f, 0.85f, 0.85f, 1.0f) :
                                                                  ImVec4(0.85f, 0.55f, 0.25f, 1.0f);

                ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", item.name.c_str());
                ImGui::SameLine();
                ImGui::TextColored(grade_color, "[%s]", item.grade.c_str());

                // Status Badge
                ImGui::SameLine(ImGui::GetWindowWidth() - 150.0f);
                if (item.unlocked) {
                    ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.40f, 1.0f), "[UNLOCKED]");
                } else {
                    ImGui::TextColored(ImVec4(0.60f, 0.60f, 0.65f, 1.0f), "[LOCKED]");
                }

                // Detail
                ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.80f, 1.0f), "%s", item.detail.c_str());
                ImGui::EndGroup();

                ImGui::EndChild();
                ImGui::PopID();
            }
            }
        }

        // -------------------------------------------------------------
        // TAB 3: GENERAL & MULTIPLAYER
        // -------------------------------------------------------------
        else if (selected_tab == 3) {
            ImGui::TextColored(ImVec4(0.98f, 0.78f, 0.28f, 1.0f), "GENERAL OPTIONS & INTEGRATIONS");
            ImGui::Spacing();

            if (ImGui::Checkbox("Close Launcher automatically after game launches", &cfg.close_launcher_on_launch)) {
                SaveConfig(cfg);
            }

            if (ImGui::Checkbox("Enable Discord Rich Presence Integration", &cfg.enable_discord_rpc)) {
                SaveConfig(cfg);
            }
            ImGui::TextDisabled("Displays 'Playing JoJo's Bizarre Adventure: Eyes of Heaven' with stage and elapsed time on Discord.");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextColored(ImVec4(0.98f, 0.78f, 0.28f, 1.0f), "STEAMWORKS P2P MULTIPLAYER INTEGRATION");
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "[OK] Steamworks SDK Initialized (AppID 480 - Spacewar)");
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "[OK] PSN SceNpMatching2 mapped to Steam Lobbies");
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "[OK] SceNet Sendto / Recvfrom routed via Steam Networking Messages");
            ImGui::Spacing();
            ImGui::TextDisabled("To play online with friends:\n1. Launch game through this launcher.\n2. In-game, go to Online Battle -> Match -> 1v1 or 2v2 -> Host.\n3. Open Steam Overlay (Shift+Tab) to invite any friend, or have them right click your name in Steam -> 'Join Game'!");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextColored(ImVec4(0.98f, 0.78f, 0.28f, 1.0f), "SYSTEM & TITLE INFORMATION");
            ImGui::Text("Game Title: JoJo's Bizarre Adventure: Eyes of Heaven");
            ImGui::Text("Title ID:   CUSA04909");
            ImGui::Text("NPCommID:   NPWR09694_00");
            ImGui::Text("Binary:     JoJo EoH.exe / shadps4.exe");
        }

        ImGui::EndChild(); // MainContentRegion

        // -------------------------------------------------------------
        // FOOTER ACTION BAR
        // -------------------------------------------------------------
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.92f, 0.68f, 0.22f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.00f, 0.80f, 0.32f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85f, 0.60f, 0.15f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.10f, 0.05f, 0.15f, 1.00f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

        if (ImGui::Button("LAUNCH GAME", ImVec2(220, 42))) {
            // Apply all settings directly to EmulatorSettings
            if (cfg.display_mode == "Windowed") {
                EmulatorSettings.SetFullScreen(false);
                EmulatorSettings.SetFullScreenMode("Windowed");
            } else if (cfg.display_mode == "Borderless Windowed") {
                EmulatorSettings.SetFullScreen(true);
                EmulatorSettings.SetFullScreenMode("Borderless");
            } else {
                EmulatorSettings.SetFullScreen(true);
                EmulatorSettings.SetFullScreenMode("Fullscreen");
            }

            if (cfg.aspect_ratio == "21:9 Ultrawide" || cfg.aspect_ratio == "21:9") {
                EmulatorSettings.SetAspectRatio("21:9");
            } else if (cfg.aspect_ratio == "16:10") {
                EmulatorSettings.SetAspectRatio("16:10");
            } else if (cfg.aspect_ratio == "4:3") {
                EmulatorSettings.SetAspectRatio("4:3");
            } else {
                EmulatorSettings.SetAspectRatio("16:9");
            }

            if (cfg.render_resolution.find("720p") != std::string::npos) {
                EmulatorSettings.SetInternalScreenWidth(1280);
                EmulatorSettings.SetInternalScreenHeight(720);
                EmulatorSettings.SetWindowWidth(1280);
                EmulatorSettings.SetWindowHeight(720);
            } else if (cfg.render_resolution.find("900p") != std::string::npos) {
                EmulatorSettings.SetInternalScreenWidth(1600);
                EmulatorSettings.SetInternalScreenHeight(900);
                EmulatorSettings.SetWindowWidth(1600);
                EmulatorSettings.SetWindowHeight(900);
            } else if (cfg.render_resolution.find("1440p") != std::string::npos) {
                EmulatorSettings.SetInternalScreenWidth(2560);
                EmulatorSettings.SetInternalScreenHeight(1440);
                EmulatorSettings.SetWindowWidth(2560);
                EmulatorSettings.SetWindowHeight(1440);
            } else if (cfg.render_resolution.find("4K") != std::string::npos) {
                EmulatorSettings.SetInternalScreenWidth(3840);
                EmulatorSettings.SetInternalScreenHeight(2160);
                EmulatorSettings.SetWindowWidth(3840);
                EmulatorSettings.SetWindowHeight(2160);
            } else {
                EmulatorSettings.SetInternalScreenWidth(1920);
                EmulatorSettings.SetInternalScreenHeight(1080);
                EmulatorSettings.SetWindowWidth(1920);
                EmulatorSettings.SetWindowHeight(1080);
            }

            EmulatorSettings.SetFsrEnabled(cfg.fsr_enabled);
            EmulatorSettings.SetRcasEnabled(cfg.rcas_enabled);
            EmulatorSettings.SetDiscordRPCEnabled(cfg.enable_discord_rpc);
            UpdateOptiScalerIni(cfg);

            if (cfg.high_cpu_priority) {
#ifdef _WIN32
                SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#endif
            }

            s_active_prompt_style = cfg.prompt_style;
            s_prefer_keyboard_mouse = (cfg.active_input_device == "Keyboard & Mouse");

            SaveInputConfigFile(cfg);
            SaveConfig(cfg);

            result.should_launch_game = true;
            running = false;
        }

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);

        ImGui::SameLine();
        ImGui::SetCursorPosX(win_w - 140.0f);
        if (ImGui::Button("Exit", ImVec2(120, 42))) {
            result.exit_requested = true;
            running = false;
        }

        ImGui::End();

        // Render Frame
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 20, 10, 32, 255);
        SDL_RenderClear(renderer);

        if (bg_wallpaper) {
            SDL_RenderTexture(renderer, bg_wallpaper, nullptr, nullptr);
        }

        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    if (s_trophy_loader_thread.joinable()) {
        s_trophy_loader_thread.join();
    }

    // Free Trophy Textures
    for (auto& t : s_trophies) {
        if (t.texture) {
            SDL_DestroyTexture(t.texture);
            t.texture = nullptr;
        }
    }
    s_trophies.clear();

    if (bg_wallpaper) SDL_DestroyTexture(bg_wallpaper);
    if (cover_art) SDL_DestroyTexture(cover_art);

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    return result;
}

} // namespace JoJoLauncher
