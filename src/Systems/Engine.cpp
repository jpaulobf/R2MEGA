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
            bool paused = false;
            std::string lastError;
            Window::Window::VideoStatus::BusAccess lastBusAccess;
            Window::Window::VideoStatus::CpuException firstCpuException;
            const auto rememberCpuException = [&]()
            {
                const auto &event = board.m68k().lastException();
                if (!event.serial || firstCpuException.valid)
                    return;
                firstCpuException.frame = board.vdp().frame();
                firstCpuException.pc = event.pc;
                firstCpuException.opcode = event.opcode;
                firstCpuException.vector = event.vector;
                firstCpuException.address = event.address;
                firstCpuException.addressValid = event.addressValid;
                firstCpuException.valid = true;
            };
            const auto rememberBusAccess = [&]()
            {
                const auto &pins = board.m68k().pins();
                if (!pins.active || !pins.addressStrobe || pins.interruptAcknowledge)
                    return;
                lastBusAccess.address = pins.address;
                lastBusAccess.data = pins.data;
                lastBusAccess.functionCode = pins.functionCode;
                lastBusAccess.valid = true;
                lastBusAccess.write = pins.write;
                lastBusAccess.byte = pins.upperDataStrobe != pins.lowerDataStrobe;
                lastBusAccess.dataValid = pins.dataValid;
            };
            Window::Window::InputCallbacks inputCallbacks;
            inputCallbacks.event = [&input](const SDL_Event &event, bool enabled)
            { input.handleEvent(event, enabled); };
            inputCallbacks.setKeyboardMapping = [&input](const KeyboardMapping &mapping)
            { input.setKeyboardMapping(mapping); };
            inputCallbacks.playerUsesJoystick = [&input](unsigned player)
            { return input.playerUsesJoystick(player); };
            inputCallbacks.setPlayerJoystick = [&input](unsigned player, bool enabled)
            { input.setPlayerJoystick(player, enabled); };
            Window::Window::EmulationCallbacks emulationCallbacks;
            emulationCallbacks.togglePause = [&running, &paused]()
            { if (running) paused = !paused; };
            emulationCallbacks.isPaused = [&paused]()
            { return paused; };
            Window::Window window([&cartridge, &board, &running, &paused, &lastError, &lastBusAccess, &firstCpuException](const std::filesystem::path &path)
                                  {
                auto loaded = Core::Cartridge::load(path);
                auto name = loaded.romName();
                cartridge = std::move(loaded);
                board.insertCartridge(*cartridge);
                lastError.clear();
                lastBusAccess = {};
                firstCpuException = {};
                running = true;
                paused = false;
                return name; }, [&cartridge, &board]() -> const std::uint32_t *
                                  { return cartridge ? board.vdp().pixels().data() : nullptr; },
                                  [&running]()
                                  { return running; }, [&board, &paused, &lastError, &lastBusAccess, &firstCpuException]()
                                  {
                Window::Window::VideoStatus status;
                status.frame = board.vdp().frame();
                status.pc = board.m68k().state().pc;
                status.opcode = board.m68k().state().prefetch[0];
                status.sr = board.m68k().state().sr;
                status.line = board.vdp().line();
                status.width = board.vdp().visibleWidth();
                status.displayEnabled = (board.vdp().registerValue(1) & 0x40) != 0;
                status.paused = paused;
                status.busAccess = lastBusAccess;
                status.exception = firstCpuException;
                status.error = lastError;
                return status; }, std::move(inputCallbacks), std::move(emulationCallbacks));
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
                if (running && !paused)
                {
                    cycleBudget = std::min(maxCyclesPerUpdate, cycleBudget + elapsed * masterClock);
                    const auto cycles = static_cast<unsigned>(cycleBudget);
                    cycleBudget -= cycles;
                    for (unsigned i = 0; i < cycles; ++i)
                    {
                        try
                        {
                            board.clock();
                            rememberBusAccess();
                            rememberCpuException();
                        }
                        catch (const std::exception &error)
                        {
                            rememberBusAccess();
                            rememberCpuException();
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