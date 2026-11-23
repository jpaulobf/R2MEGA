#pragma once
#include "Systems/KeyboardMapping.h"
#include "Core/IO/ControllerInput.h"
#include <map>
#include <memory>
#include <set>

namespace R2MEGA::Systems
{
    // Traduz teclado e gamepads SDL para os dois controles de três botões.
    class Input
    {
    public:
        Input();
        ~Input();
        Input(const Input &) = delete;
        Input &operator=(const Input &) = delete;

        bool initialize();
        void shutdown();
        void handleEvent(const SDL_Event &event, bool enabled);
        Core::ControllerInput::State update(bool enabled);
        void clear();
        void setKeyboardMapping(const KeyboardMapping &mapping);
        void setPlayerJoystick(unsigned player, bool enabled);
        bool playerUsesJoystick(unsigned player) const;
        bool fastForwardPressed() const { return fastForwardPressed_; }
        std::size_t gamepadCount() const { return gamepads_.size(); }

    private:
        struct Binding
        {
            unsigned player, button;
        };
        using Gamepad = std::unique_ptr<SDL_Gamepad, decltype(&SDL_CloseGamepad)>;
        struct Device
        {
            Gamepad handle;
            unsigned slot;
        };
        bool discoverGamepads();
        void openGamepad(SDL_JoystickID id);
        int playerForSlot(unsigned slot) const;

        std::map<SDL_Keycode, Binding> keyMap_;
        std::set<SDL_Keycode> pressedKeys_;
        std::map<SDL_JoystickID, Device> gamepads_;
        bool playerJoystick_[2]{};
        bool initialized_ = false;
        bool fastForwardPressed_ = false;
    };
}