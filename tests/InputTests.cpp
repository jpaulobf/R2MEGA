#include "Systems/Input.h"
#include "Window/KeyboardConfig.h"
#include <SDL3/SDL.h>
#include <filesystem>
#include <stdexcept>

using namespace R2MEGA;

static void require(bool condition)
{
    if (!condition)
        throw std::runtime_error("Falha no teste de entrada");
}

static void key(Systems::Input &input, SDL_Keycode code, bool down, bool enabled = true)
{
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.key = code;
    input.handleEvent(event, enabled);
}

static void pump(Systems::Input &input)
{
    SDL_UpdateJoysticks();
    SDL_Event event;
    while (SDL_PollEvent(&event))
        input.handleEvent(event, true);
}

int main()
{
    SDL_SetHintWithPriority(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,
                            "0xcafe/0xbabe", SDL_HINT_OVERRIDE);
    require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK));
    {
        Systems::Input input;
        require(!input.playerUsesJoystick(0) && !input.playerUsesJoystick(1));
        key(input, SDLK_J, true);
        key(input, SDLK_K, true);
        key(input, SDLK_L, true);
        key(input, SDLK_Z, true);
        key(input, SDLK_X, true);
        key(input, SDLK_C, true);
        auto state = input.update(true);
        require(state[0] == (Core::ControllerInput::A | Core::ControllerInput::B | Core::ControllerInput::C));
        require(state[1] == (Core::ControllerInput::A | Core::ControllerInput::B | Core::ControllerInput::C));
        require(input.update(false) == Core::ControllerInput::State{});

        auto mapping = Systems::defaultKeyboardMapping();
        mapping[0][4] = SDLK_Q;
        input.setKeyboardMapping(mapping);
        key(input, SDLK_J, true);
        require(input.update(true)[0] == 0);
        key(input, SDLK_Q, true);
        require(input.update(true)[0] == Core::ControllerInput::A);
        key(input, SDLK_Q, false);
        SDL_Event lost{};
        lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        input.handleEvent(lost, true);
        require(input.update(true)[0] == 0);

        const auto configPath = std::filesystem::current_path() / "input-test-keyboard.ini";
        Window::KeyboardConfig config(configPath);
        require(config.save(mapping));
        Window::KeyboardConfig reloaded(configPath);
        require(reloaded.load() == mapping);
        mapping = Systems::defaultKeyboardMapping();
        require(reloaded.save(mapping));
        Window::KeyboardConfig replaced(configPath);
        require(replaced.load() == mapping);

        SDL_VirtualJoystickDesc desc{};
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.vendor_id = 0xcafe;
        desc.product_id = 0xbabe;
        desc.name = "R2MEGA virtual gamepad";
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
        desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
        const auto id = SDL_AttachVirtualJoystick(&desc);
        require(id != 0 && SDL_IsGamepad(id));
        auto *virtualPad = SDL_OpenJoystick(id);
        require(virtualPad != nullptr);
        require(input.initialize());
        pump(input);
        require(input.gamepadCount() == 1);
        key(input, SDLK_Q, true);
        input.setPlayerJoystick(1, true);
        require(input.update(true)[0] == Core::ControllerInput::A);
        require(SDL_SetJoystickVirtualButton(virtualPad, SDL_GAMEPAD_BUTTON_WEST, true));
        require(SDL_SetJoystickVirtualButton(virtualPad, SDL_GAMEPAD_BUTTON_SOUTH, true));
        require(SDL_SetJoystickVirtualButton(virtualPad, SDL_GAMEPAD_BUTTON_EAST, true));
        require(SDL_SetJoystickVirtualButton(virtualPad, SDL_GAMEPAD_BUTTON_START, true));
        pump(input);
        state = input.update(true);
        require(state[0] == Core::ControllerInput::A && state[1] == (Core::ControllerInput::A | Core::ControllerInput::B |
                                                                     Core::ControllerInput::C | Core::ControllerInput::Start));
        key(input, SDLK_Q, false);
        input.setPlayerJoystick(1, false);
        input.setPlayerJoystick(0, true);
        require(input.update(true)[0] == state[1]);
        require(SDL_SetJoystickVirtualButton(virtualPad, SDL_GAMEPAD_BUTTON_WEST, false));
        require(SDL_SetJoystickVirtualButton(virtualPad, SDL_GAMEPAD_BUTTON_SOUTH, false));
        require(SDL_SetJoystickVirtualButton(virtualPad, SDL_GAMEPAD_BUTTON_EAST, false));
        require(SDL_SetJoystickVirtualButton(virtualPad, SDL_GAMEPAD_BUTTON_START, false));
        require(SDL_SetJoystickVirtualAxis(virtualPad, SDL_GAMEPAD_AXIS_LEFTX, -32768));
        pump(input);
        require(input.update(true)[0] == Core::ControllerInput::Left);
        SDL_CloseJoystick(virtualPad);
        SDL_DetachVirtualJoystick(id);
        pump(input);
        require(input.gamepadCount() == 0 && input.update(true)[0] == 0);
        input.shutdown();
    }
    SDL_Quit();
}