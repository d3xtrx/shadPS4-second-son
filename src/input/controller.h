// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <mutex>
#include "common/types.h"
#include "core/libraries/pad/pad.h"

struct SDL_Gamepad;

namespace Input {

enum class Axis {
    LeftX = 0,
    LeftY = 1,
    RightX = 2,
    RightY = 3,
    TriggerLeft = 4,
    TriggerRight = 5,

    AxisMax
};

struct TouchpadEntry {
    bool state{};
    u16 x{};
    u16 y{};
};

struct State {
    Libraries::Pad::OrbisPadButtonDataOffset buttonsState{};
    u64 time = 0;
    int axes[static_cast<int>(Axis::AxisMax)] = {128, 128, 128, 128, 0, 0};
    TouchpadEntry touchpad[2] = {{false, 0, 0}, {false, 0, 0}};
    Libraries::Pad::OrbisFVector3 acceleration = {0.0f, 0.0f, 0.0f};
    Libraries::Pad::OrbisFVector3 angularVelocity = {0.0f, 0.0f, 0.0f};
    Libraries::Pad::OrbisFQuaternion orientation = {0.0f, 0.0f, 0.0f, 1.0f};
};

inline int GetAxis(int min, int max, int value) {
    int v = (255 * (value - min)) / (max - min);
    return (v < 0 ? 0 : (v > 255 ? 255 : v));
}

constexpr u32 MAX_STATES = 64;

class GameController {
public:
    GameController();
    virtual ~GameController() = default;

    void ReadState(State* state, bool* isConnected, int* connectedCount);
    int ReadStates(State* states, int states_num, bool* isConnected, int* connectedCount);
    State GetLastState() const;
    void CheckButton(int id, Libraries::Pad::OrbisPadButtonDataOffset button, bool isPressed);
    void AddState(const State& state);
    void Axis(int id, Input::Axis axis, int value);
    void Gyro(int id, const float gyro[3]);
    void Acceleration(int id, const float acceleration[3]);
    void SetLightBarRGB(u8 r, u8 g, u8 b);
    bool SetVibration(u8 smallMotor, u8 largeMotor);
    void SetTouchpadState(int touchIndex, bool touchDown, float x, float y);
    void TryOpenSDLController();
    u32 Poll();

    float gyro_poll_rate;
    float accel_poll_rate;
    static void CalculateOrientation(Libraries::Pad::OrbisFVector3& acceleration,
                                     Libraries::Pad::OrbisFVector3& angularVelocity,
                                     float deltaTime,
                                     Libraries::Pad::OrbisFQuaternion& orientation);

private:
    struct StateInternal {
        bool obtained = false;
    };

    std::mutex m_mutex;
    bool m_connected = true;
    State m_last_state;
    int m_connected_count = 0;
    u32 m_states_num = 0;
    u32 m_first_state = 0;
    std::array<State, MAX_STATES> m_states;
    std::array<StateInternal, MAX_STATES> m_private;

    SDL_Gamepad* m_sdl_gamepad = nullptr;
};

} // namespace Input
