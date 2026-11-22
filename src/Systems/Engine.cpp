#include "Systems/Engine.h"
#include "Systems/Input.h"
#include "Window/Window.h"
#include "Core/Cartridge/Cartridge.h"
#include "Core/Board.h"
#include <optional>
#include <algorithm>
#include <chrono>
#include <exception>
#include <iostream>

namespace R2MEGA::Systems
{
    int Engine::run()
    {
        try
        {
            std::optional<Core::Cartridge> cartridge;
            Core::Board board;
            Input input;
            bool running = false;
            std::string lastError;
            Window::Window::InputCallbacks inputCallbacks;
            inputCallbacks.event = [&input](const SDL_Event &event, bool enabled)
            { input.handleEvent(event, enabled); };
            inputCallbacks.setKeyboardMapping = [&input](const KeyboardMapping &mapping)
            { input.setKeyboardMapping(mapping); };
            inputCallbacks.playerUsesJoystick = [&input](unsigned player)
            { return input.playerUsesJoystick(player); };
            inputCallbacks.setPlayerJoystick = [&input](unsigned player, bool enabled)
            { input.setPlayerJoystick(player, enabled); };
            Window::Window window([&cartridge, &board, &running, &lastError](const std::filesystem::path &path)
                                  {
                auto loaded = Core::Cartridge::load(path);
                auto name = loaded.romName();
                cartridge = std::move(loaded);
                board.insertCartridge(*cartridge);
                lastError.clear();
                running = true;
                return name; }, [&cartridge, &board]() -> const std::uint32_t *
                                  { return cartridge ? board.vdp().pixels().data() : nullptr; },
                                  [&running]()
                                  { return running; }, [&board, &lastError]()
                                  {
                Window::Window::VideoStatus status;
                status.frame = board.vdp().frame();
                status.pc = board.m68k().state().pc;
                status.width = board.vdp().visibleWidth();
                status.displayEnabled = (board.vdp().registerValue(1) & 0x40) != 0;
                status.error = lastError;
                return status; }, std::move(inputCallbacks));
            window.open();
            if (!input.initialize())
                std::cerr << "R2MEGA: gamepads indisponíveis: " << SDL_GetError() << '\n';
            constexpr double masterClock = 53693175.0;
            // Limita a recuperação após pausas longas para manter a janela responsiva.
            constexpr double maxCyclesPerUpdate = masterClock / 30.0;
            auto previousTime = std::chrono::steady_clock::now();
            double cycleBudget = 0;
            while (window.processEvents())
            {
                const auto buttons = input.update(window.acceptsInput());
                for (unsigned player = 0; player < buttons.size(); ++player)
                    board.setControllerButtons(player, buttons[player]);
                const auto now = std::chrono::steady_clock::now();
                const double elapsed = std::chrono::duration<double>(now - previousTime).count();
                previousTime = now;
                if (running)
                {
                    cycleBudget = std::min(maxCyclesPerUpdate, cycleBudget + elapsed * masterClock);
                    const auto cycles = static_cast<unsigned>(cycleBudget);
                    cycleBudget -= cycles;
                    for (unsigned i = 0; i < cycles; ++i)
                    {
                        try
                        {
                            board.clock();
                        }
                        catch (const std::exception &error)
                        {
                            running = false;
                            lastError = error.what();
                            break;
                        }
                    }
                }
                else
                    cycleBudget = 0;
                window.render();
            }
            input.shutdown();
            return 0;
        }
        catch (const std::exception &error)
        {
            std::cerr << "R2MEGA: " << error.what() << '\n';
            return 1;
        }
    }
}