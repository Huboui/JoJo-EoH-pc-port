// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_set>
#include <utility>

#include <SDL3/SDL.h>

#include "common/logging/log.h"
#include "core/emulator_settings.h"
#include "core/libraries/kernel/time.h"
#include "core/libraries/system/userservice.h"
#include "core/user_settings.h"
#include "input/controller.h"
#include "launcher/jojo_launcher.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xinput.h>

typedef DWORD(WINAPI* PFN_XInputGetState)(DWORD dwUserIndex, XINPUT_STATE* pState);
static PFN_XInputGetState s_pfnXInputGetState = nullptr;
static bool s_xinput_initialized = false;

static void InitXInput() {
    if (s_xinput_initialized) {
        return;
    }
    s_xinput_initialized = true;
    HMODULE h = LoadLibraryA("xinput1_4.dll");
    if (!h) h = LoadLibraryA("xinput1_3.dll");
    if (!h) h = LoadLibraryA("xinput9_1_0.dll");
    if (h) {
        s_pfnXInputGetState = reinterpret_cast<PFN_XInputGetState>(GetProcAddress(h, "XInputGetState"));
    }
}
#endif

namespace Input {

using Libraries::Pad::OrbisPadButtonDataOffset;

namespace {

void CalculateOrientation(const Libraries::Pad::OrbisFVector3& angular_velocity, float delta_time,
                          const Libraries::Pad::OrbisFQuaternion& last_orientation,
                          Libraries::Pad::OrbisFQuaternion& orientation) {
    if (delta_time > 1.0f) {
        orientation = last_orientation;
        return;
    }
    Libraries::Pad::OrbisFQuaternion q = last_orientation;
    const Libraries::Pad::OrbisFQuaternion omega = {angular_velocity.x, angular_velocity.y,
                                                    angular_velocity.z, 0.0f};

    const Libraries::Pad::OrbisFQuaternion q_omega = {
        q.w * omega.x + q.x * omega.w + q.y * omega.z - q.z * omega.y,
        q.w * omega.y + q.y * omega.w + q.z * omega.x - q.x * omega.z,
        q.w * omega.z + q.z * omega.w + q.x * omega.y - q.y * omega.x,
        q.w * omega.w - q.x * omega.x - q.y * omega.y - q.z * omega.z};

    const Libraries::Pad::OrbisFQuaternion q_dot = {0.5f * q_omega.x, 0.5f * q_omega.y,
                                                    0.5f * q_omega.z, 0.5f * q_omega.w};

    q.x += q_dot.x * delta_time;
    q.y += q_dot.y * delta_time;
    q.z += q_dot.z * delta_time;
    q.w += q_dot.w * delta_time;

    const float norm = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    q.x /= norm;
    q.y /= norm;
    q.z /= norm;
    q.w /= norm;
    orientation = q;
}

} // namespace

GameController::GameController() : m_states_queue(64) {}

State GameController::ReadState() {
    std::lock_guard lock{m_state_mutex};
    return m_state;
}

int GameController::ReadStates(State* states, int states_num) {
    std::lock_guard lock{m_state_mutex};
    if (states_num <= 0) {
        return 0;
    }

    if (!m_state.connected) {
        states[0] = m_state;
        return 1;
    }

    if (states_num == 1) {
        // Retained history can make a later multi-sample read return up to 64 stale reports, so
        // mixed single- and multi-sample reads require dedicated tests.
        states[0] = m_state;
        return 1;
    }

    int read_count = 0;
    while (read_count < states_num) {
        auto state = m_states_queue.Pop();
        if (!state) {
            break;
        }
        states[read_count++] = std::move(*state);
    }
    if (read_count == 0) {
        states[0] = m_state;
        return 1;
    }
    return read_count;
}

void GameController::Button(OrbisPadButtonDataOffset button, bool is_pressed) {
    std::lock_guard lock{m_state_mutex};
    m_state.OnButton(button, is_pressed);
    PushStateLocked();
}

void GameController::Axis(Input::Axis axis, int value, bool smooth) {
    std::lock_guard lock{m_state_mutex};
    const u64 timestamp = Libraries::Kernel::sceKernelGetProcessTime();
    m_state.OnAxis(axis, value, timestamp, smooth);
    PushStateLocked(timestamp);
}

void GameController::UpdateGyro(const float gyro[3]) {
    std::lock_guard lock{m_state_mutex};
    std::memcpy(gyro_buf, gyro, sizeof(gyro_buf));
}

void GameController::UpdateAcceleration(const float acceleration[3]) {
    std::lock_guard lock{m_state_mutex};
    std::memcpy(accel_buf, acceleration, sizeof(accel_buf));
}

void GameController::PollDirectHardware() {
    std::lock_guard lock{m_state_mutex};
    if (JoJoLauncher::IsPreferKeyboardMouse()) {
        return;
    }

#ifdef _WIN32
    InitXInput();
#endif

    bool have_input = false;
    u32 buttons = 0;
    s32 lx = 128, ly = 128, rx = 128, ry = 128;
    s32 l2_val = 0, r2_val = 0;
    bool is_touchpad = false;

#ifdef _WIN32
    if (s_pfnXInputGetState) {
        int xslot = (user_id >= 1000 && user_id < 1004) ? (user_id - 1000) : 0;
        XINPUT_STATE xstate;
        DWORD ret = s_pfnXInputGetState(xslot, &xstate);
        if (ret != ERROR_SUCCESS && xslot != 0) {
            ret = s_pfnXInputGetState(0, &xstate);
        }
        if (ret == ERROR_SUCCESS) {
            have_input = true;
            const WORD w = xstate.Gamepad.wButtons;
            if (w & XINPUT_GAMEPAD_DPAD_UP) buttons |= std::to_underlying(OrbisPadButtonDataOffset::Up);
            if (w & XINPUT_GAMEPAD_DPAD_DOWN) buttons |= std::to_underlying(OrbisPadButtonDataOffset::Down);
            if (w & XINPUT_GAMEPAD_DPAD_LEFT) buttons |= std::to_underlying(OrbisPadButtonDataOffset::Left);
            if (w & XINPUT_GAMEPAD_DPAD_RIGHT) buttons |= std::to_underlying(OrbisPadButtonDataOffset::Right);

            if (w & XINPUT_GAMEPAD_A) buttons |= std::to_underlying(OrbisPadButtonDataOffset::Cross);
            if (w & XINPUT_GAMEPAD_B) buttons |= std::to_underlying(OrbisPadButtonDataOffset::Circle);
            if (w & XINPUT_GAMEPAD_X) buttons |= std::to_underlying(OrbisPadButtonDataOffset::Square);
            if (w & XINPUT_GAMEPAD_Y) buttons |= std::to_underlying(OrbisPadButtonDataOffset::Triangle);

            if (w & XINPUT_GAMEPAD_LEFT_SHOULDER) buttons |= std::to_underlying(OrbisPadButtonDataOffset::L1);
            if (w & XINPUT_GAMEPAD_RIGHT_SHOULDER) buttons |= std::to_underlying(OrbisPadButtonDataOffset::R1);
            if (w & XINPUT_GAMEPAD_LEFT_THUMB) buttons |= std::to_underlying(OrbisPadButtonDataOffset::L3);
            if (w & XINPUT_GAMEPAD_RIGHT_THUMB) buttons |= std::to_underlying(OrbisPadButtonDataOffset::R3);
            if (w & XINPUT_GAMEPAD_START) buttons |= std::to_underlying(OrbisPadButtonDataOffset::Options);

            // Map Xbox Back / View / Select to PS4 TouchPad click
            if (w & XINPUT_GAMEPAD_BACK) {
                buttons |= std::to_underlying(OrbisPadButtonDataOffset::TouchPad);
                is_touchpad = true;
            }

            l2_val = xstate.Gamepad.bLeftTrigger;
            r2_val = xstate.Gamepad.bRightTrigger;
            if (l2_val > 30) buttons |= std::to_underlying(OrbisPadButtonDataOffset::L2);
            if (r2_val > 30) buttons |= std::to_underlying(OrbisPadButtonDataOffset::R2);

            lx = std::clamp(static_cast<int>(std::round(((float)xstate.Gamepad.sThumbLX + 32768.0f) * 255.0f / 65535.0f)), 0, 255);
            ly = std::clamp(static_cast<int>(std::round((32767.0f - (float)xstate.Gamepad.sThumbLY) * 255.0f / 65535.0f)), 0, 255);

            rx = std::clamp(static_cast<int>(std::round(((float)xstate.Gamepad.sThumbRX + 32768.0f) * 255.0f / 65535.0f)), 0, 255);
            ry = std::clamp(static_cast<int>(std::round((32767.0f - (float)xstate.Gamepad.sThumbRY) * 255.0f / 65535.0f)), 0, 255);
        }
    }
#endif

    if (m_sdl_gamepad) {
        SDL_UpdateGamepads();
        if (SDL_GamepadConnected(m_sdl_gamepad)) {
            have_input = true;
            u32 sdl_buttons = 0;
            if (SDL_GetGamepadButton(m_sdl_gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) sdl_buttons |= std::to_underlying(OrbisPadButtonDataOffset::Up);
            if (SDL_GetGamepadButton(m_sdl_gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) sdl_buttons |= std::to_underlying(OrbisPadButtonDataOffset::Down);
            if (SDL_GetGamepadButton(m_sdl_gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) sdl_buttons |= std::to_underlying(OrbisPadButtonDataOffset::Left);
            if (SDL_GetGamepadButton(m_sdl_gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) sdl_buttons |= std::to_underlying(OrbisPadButtonDataOffset::Right);

            if (SDL_GetGamepadButton(m_sdl_gamepad, SDL_GAMEPAD_BUTTON_SOUTH)) sdl_buttons |= std::to_underlying(OrbisPadButtonDataOffset::Cross);
            if (SDL_GetGamepadButton(m_sdl_gamepad, SDL_GAMEPAD_BUTTON_EAST)) sdl_buttons |= std::to_underlying(OrbisPadButtonDataOffset::Circle);
            if (SDL_GetGamepadButton(m_sdl_gamepad, SDL_GAMEPAD_BUTTON_WEST)) sdl_buttons |= std::to_underlying(OrbisPadButtonDataOffset::Square);
            if (SDL_GetGamepadButton(m_sdl_gamepad, SDL_GAMEPAD_BUTTON_NORTH)) sdl_buttons |= std::to_underlying(OrbisPadButtonDataOffset::Triangle);

            if (SDL_GetGamepadButton(m_sdl_gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) sdl_buttons |= std::to_underlying(OrbisPadButtonDataOffset::L1);
            if (SDL_GetGamepadButton(m_sdl_gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) sdl_buttons |= std::to_underlying(OrbisPadButtonDataOffset::R1);
            if (SDL_GetGamepadButton(m_sdl_gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK)) sdl_buttons |= std::to_underlying(OrbisPadButtonDataOffset::L3);
            if (SDL_GetGamepadButton(m_sdl_gamepad, SDL_GAMEPAD_BUTTON_RIGHT_STICK)) sdl_buttons |= std::to_underlying(OrbisPadButtonDataOffset::R3);
            if (SDL_GetGamepadButton(m_sdl_gamepad, SDL_GAMEPAD_BUTTON_START)) sdl_buttons |= std::to_underlying(OrbisPadButtonDataOffset::Options);

            const bool sdl_touchpad = SDL_GetGamepadButton(m_sdl_gamepad, SDL_GAMEPAD_BUTTON_TOUCHPAD);
            const bool sdl_back = SDL_GetGamepadButton(m_sdl_gamepad, SDL_GAMEPAD_BUTTON_BACK);
            const bool sdl_misc1 = SDL_GetGamepadButton(m_sdl_gamepad, SDL_GAMEPAD_BUTTON_MISC1);
            if (sdl_touchpad || sdl_back || sdl_misc1) {
                sdl_buttons |= std::to_underlying(OrbisPadButtonDataOffset::TouchPad);
                is_touchpad = true;
            }

            const Sint16 sdl_l2_raw = SDL_GetGamepadAxis(m_sdl_gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
            const Sint16 sdl_r2_raw = SDL_GetGamepadAxis(m_sdl_gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
            if (sdl_l2_raw > 8000) sdl_buttons |= std::to_underlying(OrbisPadButtonDataOffset::L2);
            if (sdl_r2_raw > 8000) sdl_buttons |= std::to_underlying(OrbisPadButtonDataOffset::R2);

            buttons |= sdl_buttons;

            auto map_trigger = [](Sint16 val) -> s32 {
                int v = static_cast<int>(std::round((float)val * 255.0f / 32767.0f));
                return std::clamp(v, 0, 255);
            };
            l2_val = std::max(l2_val, map_trigger(sdl_l2_raw));
            r2_val = std::max(r2_val, map_trigger(sdl_r2_raw));

            auto map_stick = [](Sint16 val) -> s32 {
                int v = static_cast<int>(std::round(((float)val + 32768.0f) * 255.0f / 65535.0f));
                return std::clamp(v, 0, 255);
            };
            const Sint16 sdl_lx = SDL_GetGamepadAxis(m_sdl_gamepad, SDL_GAMEPAD_AXIS_LEFTX);
            const Sint16 sdl_ly = SDL_GetGamepadAxis(m_sdl_gamepad, SDL_GAMEPAD_AXIS_LEFTY);
            const Sint16 sdl_rx = SDL_GetGamepadAxis(m_sdl_gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
            const Sint16 sdl_ry = SDL_GetGamepadAxis(m_sdl_gamepad, SDL_GAMEPAD_AXIS_RIGHTY);

            if (std::abs((int)sdl_lx) > 4000 || std::abs((int)sdl_ly) > 4000) {
                lx = map_stick(sdl_lx);
                ly = map_stick(sdl_ly);
            }
            if (std::abs((int)sdl_rx) > 4000 || std::abs((int)sdl_ry) > 4000) {
                rx = map_stick(sdl_rx);
                ry = map_stick(sdl_ry);
            }
        }
    }

    if (!have_input) {
        return;
    }

    m_state.buttonsState = static_cast<OrbisPadButtonDataOffset>(buttons);
    m_state.axes[std::to_underlying(Axis::LeftX)] = lx;
    m_state.axes[std::to_underlying(Axis::LeftY)] = ly;
    m_state.axes[std::to_underlying(Axis::RightX)] = rx;
    m_state.axes[std::to_underlying(Axis::RightY)] = ry;
    m_state.axes[std::to_underlying(Axis::TriggerLeft)] = l2_val;
    m_state.axes[std::to_underlying(Axis::TriggerRight)] = r2_val;

    m_state.touchpad[0].state = is_touchpad;
    if (is_touchpad) {
        m_state.touchpad[0].ID = 1;
        m_state.touchpad[0].x = 960;
        m_state.touchpad[0].y = 471;
    }

    m_state.connected = true;
    if (m_state.connected_count == 0) {
        m_state.connected_count = 1;
    }

    const u64 timestamp = Libraries::Kernel::sceKernelGetProcessTime();
    m_state.time = timestamp;
    PushStateLocked(timestamp);

    static u32 s_last_buttons = 0;
    if (buttons != s_last_buttons) {
        if (buttons != 0) {
            LOG_INFO(Input, "[ControllerDirect] Buttons pressed: 0x{:08X}", buttons);
        }
        s_last_buttons = buttons;
    }
}

void GameController::PollState() {
    std::lock_guard lock{m_state_mutex};
    PushStateLocked();
}

void GameController::ResetOrientation() {
    std::lock_guard lock{m_state_mutex};
    m_state.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    m_last_orientation_update = 0;
    PushStateLocked();
}

void GameController::SetTouchpadState(int touch_index, bool touch_down, float x, float y) {
    if (touch_index < 0 || touch_index >= 2) {
        return;
    }

    std::lock_guard lock{m_state_mutex};
    const u64 timestamp = Libraries::Kernel::sceKernelGetProcessTime();
    const bool was_pressed = m_state.touchpad[0].state || m_state.touchpad[1].state;
    auto& touch = m_state.touchpad[touch_index];
    if (touch_down && !touch.state) {
        touch.ID = m_next_touch_id;
        m_next_touch_id = m_next_touch_id == 127 ? 1 : m_next_touch_id + 1;
    }
    m_state.OnTouchpad(touch_index, touch_down, x, y);
    const bool is_pressed = m_state.touchpad[0].state || m_state.touchpad[1].state;
    if (!was_pressed && is_pressed) {
        m_touch_down_timestamp = timestamp;
    } else if (was_pressed && !is_pressed) {
        m_touch_down_timestamp = 0;
    }
    PushStateLocked(timestamp);
}

void GameController::ConnectController(SDL_Gamepad* pad) {
    std::lock_guard lock{m_state_mutex};
    m_sdl_gamepad = pad;
    if (override_colour) {
        SetLightBarRGB({});
    }
    m_states_queue.Clear();
    if (!m_state.connected) {
        ++m_state.connected_count;
        if (m_state.connected_count == 0) {
            m_state.connected_count = 1;
        }
    }
    m_state.connected = true;
    m_last_orientation_update = 0;
    PushStateLocked();
}

void GameController::DisconnectController() {
    std::lock_guard lock{m_state_mutex};
    m_states_queue.Clear();
    m_sdl_gamepad = nullptr;

    const u8 connected_count = m_state.connected_count;
    m_state = {};
    m_state.connected_count = connected_count;
    std::fill(gyro_buf, gyro_buf + 3, 0.0f);
    std::fill(accel_buf, accel_buf + 3, 0.0f);
    accel_buf[1] = 9.81f;
    m_next_touch_id = 1;
    m_touch_down_timestamp = 0;
    m_state.connected = false;
    m_last_orientation_update = 0;
    PushStateLocked();
}

void GameController::UpdateOrientationLocked(u64 timestamp) {
    if (m_last_orientation_update == 0 || timestamp <= m_last_orientation_update) {
        m_last_orientation_update = timestamp;
        return;
    }
    const float delta_time =
        static_cast<float>(timestamp - m_last_orientation_update) / 1'000'000.f;
    Libraries::Pad::OrbisFQuaternion orientation{};
    CalculateOrientation(m_state.angularVelocity, delta_time, m_state.orientation, orientation);
    m_state.orientation = orientation;
    m_last_orientation_update = timestamp;
}

void GameController::PushStateLocked(u64 timestamp) {
    if (timestamp == 0) {
        timestamp = Libraries::Kernel::sceKernelGetProcessTime();
    }
    m_state.UpdateAxisSmoothing(timestamp);
    m_state.OnGyro(gyro_buf);
    m_state.OnAccel(accel_buf);
    UpdateOrientationLocked(timestamp);
    m_state.time = timestamp;
    m_state.touch_time_since_held_down =
        m_touch_down_timestamp == 0 ? 0 : timestamp - m_touch_down_timestamp;
    m_states_queue.Push(m_state);
}

void GameController::SetLightBarRGB(u8 const r, u8 const g, u8 const b) {
    if (override_colour.has_value()) {
        if (m_sdl_gamepad) {
            SDL_SetGamepadLED(m_sdl_gamepad, override_colour->r, override_colour->g,
                              override_colour->b);
        }
        return;
    }
    colour = {r, g, b};
    if (m_sdl_gamepad != nullptr) {
        SDL_SetGamepadLED(m_sdl_gamepad, r, g, b);
    }
}

void GameController::SetLightBarRGB(Colour const c) {
    SetLightBarRGB(c.r, c.g, c.b);
}

Colour GameController::GetLightBarRGB() {
    return colour;
}

void GameController::PollLightColour() {
    if (m_sdl_gamepad != nullptr) {
        SDL_SetGamepadLED(m_sdl_gamepad, colour.r, colour.g, colour.b);
    }
}

void GameControllers::ResetLightbarColors() {
    for (auto& c : controllers) {
        auto const* u = UserManagement.GetUserByID(c->user_id);
        if (!u || !c->m_sdl_gamepad) {
            continue;
        }
        auto const i = u->user_color - 1;
        if (i < 0 || i > 3) {
            continue;
        }
        auto const& col = g_user_colours[i];
        c->override_colour = std::nullopt;
        c->SetLightBarRGB(col);
    }
}

bool GameController::SetVibration(u8 smallMotor, u8 largeMotor) {
    if (m_sdl_gamepad != nullptr) {
        return SDL_RumbleGamepad(m_sdl_gamepad, (smallMotor / 255.0f) * 0xFFFF,
                                 (largeMotor / 255.0f) * 0xFFFF, -1);
    }
    return true;
}

static bool is_first_check = true;

void GameControllers::TryOpenSDLControllers() {
    using namespace Libraries::UserService;
    SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK);
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_XINPUT_ENABLED, "1");

    int controller_count = 0;
    SDL_JoystickID* new_joysticks = SDL_GetGamepads(&controller_count);
    LOG_INFO(Input, "{} controllers are currently connected", controller_count);

    std::unordered_set<SDL_JoystickID> assigned_ids;
    std::array<bool, 4> slot_taken{false, false, false, false};

    for (int i = 0; i < 4; i++) {
        SDL_Gamepad* pad = controllers[i]->m_sdl_gamepad;
        if (pad) {
            SDL_JoystickID id = SDL_GetGamepadID(pad);
            bool still_connected = false;
            if (new_joysticks != nullptr) {
                for (int j = 0; j < controller_count; j++) {
                    if (new_joysticks[j] == id) {
                        still_connected = true;
                        assigned_ids.insert(id);
                        slot_taken[i] = true;
                        break;
                    }
                }
            }
            if (!still_connected) {
                if (!SDL_GamepadConnected(pad)) {
                    LOG_INFO(Input, "Gamepad in slot {} disconnected", i);
                    SDL_CloseGamepad(pad);
                    controllers[i]->DisconnectController();
                    controllers[i]->user_id = -1;
                    slot_taken[i] = false;
                } else {
                    assigned_ids.insert(id);
                    slot_taken[i] = true;
                }
            }
        }
    }

    if (new_joysticks != nullptr) {
        for (int j = 0; j < controller_count; j++) {
            SDL_JoystickID id = new_joysticks[j];
            if (assigned_ids.contains(id))
                continue;

            SDL_Gamepad* pad = SDL_OpenGamepad(id);
            if (!pad) {
                LOG_ERROR(Input, "Failed to open gamepad handle {}", id);
                continue;
            }

            for (int i = 0; i < 4; i++) {
                if (!slot_taken[i]) {
                    auto u = UserManagement.GetUserByPlayerIndex(i + 1);
                    s32 uid = u ? u->user_id : (1000 + i);
                    auto* c = controllers[i].get();
                    const char* pad_name = SDL_GetGamepadName(pad);
                    SDL_GamepadType pad_type = SDL_GetGamepadType(pad);
                    LOG_INFO(Input, "[Input] Gamepad registered for slot {} (Player {}): '{}', Type={}, Handle={}",
                             i, i + 1, pad_name ? pad_name : "Unknown", (int)pad_type, SDL_GetGamepadID(pad));
                    slot_taken[i] = true;
                    c->user_id = uid;
                    if (u) {
                        UserManagement.LoginUser(u, i + 1);
                    }
                    c->ConnectController(pad);
                    assigned_ids.insert(id);

                    if (EmulatorSettings.IsMotionControlsEnabled()) {
                        if (SDL_SetGamepadSensorEnabled(c->m_sdl_gamepad, SDL_SENSOR_GYRO, true)) {
                            const float poll_rate =
                                SDL_GetGamepadSensorDataRate(c->m_sdl_gamepad, SDL_SENSOR_GYRO);
                            LOG_INFO(Input, "Gyro initialized, poll rate: {}", poll_rate);
                        }
                        if (SDL_SetGamepadSensorEnabled(c->m_sdl_gamepad, SDL_SENSOR_ACCEL, true)) {
                            const float poll_rate =
                                SDL_GetGamepadSensorDataRate(c->m_sdl_gamepad, SDL_SENSOR_ACCEL);
                            LOG_INFO(Input, "Accel initialized, poll rate: {}", poll_rate);
                        }
                    }
                    break;
                }
            }
        }
    }

    if (controllers[0]->user_id == -1 ||
        controllers[0]->user_id == Libraries::UserService::ORBIS_USER_SERVICE_USER_ID_INVALID) {
        auto u = UserManagement.GetUserByPlayerIndex(1);
        controllers[0]->user_id = u ? u->user_id : 1000;
        if (!controllers[0]->m_state.connected) {
            controllers[0]->ConnectController(controllers[0]->m_sdl_gamepad);
        }
        if (u) {
            UserManagement.LoginUser(u, 1);
        }
    }

    if (new_joysticks) {
        SDL_free(new_joysticks);
        new_joysticks = nullptr;
    }
}

u8 GameControllers::GetGamepadIndexFromJoystickId(SDL_JoystickID id) {
    auto g = SDL_GetGamepadFromID(id);
    if (!g) {
        return 0;
    }
    for (int i = 0; i < 5; i++) {
        if (controllers[i]->m_sdl_gamepad == g) {
            return i;
        }
    }
    return 0;
}

std::optional<u8> GameControllers::GetControllerIndexFromUserID(s32 user_id) {
    auto const u = UserManagement.GetUserByID(user_id);
    if (!u) {
        return std::nullopt;
    }
    return u->player_index - 1;
}

std::optional<u8> GameControllers::GetControllerIndexFromControllerID(s32 controller_id) {
    if (controller_id < 1 || controller_id > 5) {
        return std::nullopt;
    }
    return controller_id - 1;
}

} // namespace Input
