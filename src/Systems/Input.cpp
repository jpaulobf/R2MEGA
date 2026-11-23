#include "Systems/Input.h"
#include <iostream>
#include <stdexcept>

namespace R2MEGA::Systems
{
    Input::Input() { setKeyboardMapping(defaultKeyboardMapping()); }
    Input::~Input() { shutdown(); }

    bool Input::initialize()
    {
        if (initialized_)
            return discoverGamepads();
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
            return false;
        initialized_ = true;
        return discoverGamepads();
    }

    bool Input::discoverGamepads()
    {
        int count = 0;
        auto *ids = SDL_GetGamepads(&count);
        if (!ids)
            return false;
        const std::unique_ptr<SDL_JoystickID, decltype(&SDL_free)> devices(ids, SDL_free);
        for (int i = 0; i < count; ++i)
            openGamepad(ids[i]);
        return true;
    }

    void Input::shutdown()
    {
        gamepads_.clear();
        clear();
        if (initialized_)
            SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
        initialized_ = false;
    }

    void Input::clear()
    {
        pressedKeys_.clear();
        fastForwardPressed_ = false;
    }

    void Input::setKeyboardMapping(const KeyboardMapping &mapping)
    {
        clear();
        keyMap_.clear();
        constexpr unsigned buttons[] = {Core::ControllerInput::Up, Core::ControllerInput::Down,
                                        Core::ControllerInput::Left, Core::ControllerInput::Right,
                                        Core::ControllerInput::A, Core::ControllerInput::B,
                                        Core::ControllerInput::C, Core::ControllerInput::Start};
        for (unsigned player = 0; player < 2; ++player)
            for (unsigned index = 0; index < 8; ++index)
                if (mapping[player][index])
                    keyMap_.insert_or_assign(mapping[player][index], Binding{player, buttons[index]});
    }

    void Input::setPlayerJoystick(unsigned player, bool enabled)
    {
        if (player >= 2)
            throw std::out_of_range("Jogador fora das portas de controle");
        if (playerJoystick_[player] == enabled)
            return;
        playerJoystick_[player] = enabled;
        for (auto it = pressedKeys_.begin(); it != pressedKeys_.end();)
            if (keyMap_.at(*it).player == player)
                it = pressedKeys_.erase(it);
            else
                ++it;
    }

    bool Input::playerUsesJoystick(unsigned player) const
    {
        if (player >= 2)
            throw std::out_of_range("Jogador fora das portas de controle");
        return playerJoystick_[player];
    }

    int Input::playerForSlot(unsigned slot) const
    {
        for (unsigned player = 0; player < 2; ++player)
            if (playerJoystick_[player] && slot-- == 0)
                return int(player);
        return -1;
    }

    void Input::openGamepad(SDL_JoystickID id)
    {
        if (!initialized_ || gamepads_.count(id) || gamepads_.size() >= 2)
            return;
        bool used[2]{};
        for (const auto &entry : gamepads_)
            used[entry.second.slot] = true;
        Gamepad pad(SDL_OpenGamepad(id), SDL_CloseGamepad);
        if (!pad)
        {
            std::cerr << "R2MEGA: falha ao abrir controle: " << SDL_GetError() << '\n';
            return;
        }
        const unsigned slot = used[0] ? 1 : 0;
        SDL_SetGamepadPlayerIndex(pad.get(), int(slot));
        gamepads_.emplace(id, Device{std::move(pad), slot});
    }

    void Input::handleEvent(const SDL_Event &event, bool enabled)
    {
        if (event.type == SDL_EVENT_GAMEPAD_ADDED || event.type == SDL_EVENT_GAMEPAD_REMAPPED)
            openGamepad(event.gdevice.which);
        else if (event.type == SDL_EVENT_GAMEPAD_REMOVED)
        {
            const auto removed = gamepads_.find(event.gdevice.which);
            if (removed != gamepads_.end())
            {
                const unsigned slot = removed->second.slot;
                gamepads_.erase(removed);
                for (auto &entry : gamepads_)
                    if (entry.second.slot > slot)
                    {
                        --entry.second.slot;
                        SDL_SetGamepadPlayerIndex(entry.second.handle.get(), int(entry.second.slot));
                    }
            }
            if (initialized_)
                discoverGamepads();
        }
        else if (event.type == SDL_EVENT_KEY_UP)
        {
            pressedKeys_.erase(event.key.key);
            if (event.key.key == SDLK_TAB)
                fastForwardPressed_ = false;
        }
        else if (event.type == SDL_EVENT_KEY_DOWN && enabled && !event.key.repeat &&
                 !(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)))
        {
            if (event.key.key == SDLK_TAB)
                fastForwardPressed_ = true;
            if (keyMap_.count(event.key.key))
                pressedKeys_.insert(event.key.key);
        }
        if (!enabled || event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_MINIMIZED)
            clear();
    }

    Core::ControllerInput::State Input::update(bool enabled)
    {
        Core::ControllerInput::State result{};
        if (!enabled)
        {
            clear();
            return result;
        }
        for (const auto key : pressedKeys_)
        {
            const auto &binding = keyMap_.at(key);
            if (!playerJoystick_[binding.player])
                result[binding.player] |= binding.button;
        }
        for (const auto &entry : gamepads_)
        {
            const auto &device = entry.second;
            const int player = playerForSlot(device.slot);
            if (player < 0 || !SDL_GamepadConnected(device.handle.get()))
                continue;
            auto &buttons = result[unsigned(player)];
            constexpr struct
            {
                SDL_GamepadButton physical;
                unsigned logical;
            } mapping[] = {
                {SDL_GAMEPAD_BUTTON_DPAD_UP, Core::ControllerInput::Up},
                {SDL_GAMEPAD_BUTTON_DPAD_DOWN, Core::ControllerInput::Down},
                {SDL_GAMEPAD_BUTTON_DPAD_LEFT, Core::ControllerInput::Left},
                {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, Core::ControllerInput::Right},
                {SDL_GAMEPAD_BUTTON_WEST, Core::ControllerInput::A},
                {SDL_GAMEPAD_BUTTON_SOUTH, Core::ControllerInput::B},
                {SDL_GAMEPAD_BUTTON_EAST, Core::ControllerInput::C},
                {SDL_GAMEPAD_BUTTON_START, Core::ControllerInput::Start}};
            for (const auto &item : mapping)
                if (SDL_GetGamepadButton(device.handle.get(), item.physical))
                    buttons |= item.logical;
            constexpr int deadZone = 16000;
            const auto x = SDL_GetGamepadAxis(device.handle.get(), SDL_GAMEPAD_AXIS_LEFTX);
            const auto y = SDL_GetGamepadAxis(device.handle.get(), SDL_GAMEPAD_AXIS_LEFTY);
            if (x < -deadZone)
                buttons |= Core::ControllerInput::Left;
            if (x > deadZone)
                buttons |= Core::ControllerInput::Right;
            if (y < -deadZone)
                buttons |= Core::ControllerInput::Up;
            if (y > deadZone)
                buttons |= Core::ControllerInput::Down;
        }
        return result;
    }
}