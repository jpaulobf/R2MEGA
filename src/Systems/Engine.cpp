#include "Systems/Engine.h"
#include "Systems/FrameCounter.h"
#include "Systems/Input.h"
#include "Window/Window.h"
#include "Core/Cartridge/Cartridge.h"
#include "Core/Board.h"
#include <optional>
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
            using Clock = FrameCounter::Clock;
            FrameCounter frameCounter;
            Clock::time_point pauseStarted;
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
            emulationCallbacks.togglePause = [&]()
            {
                if (!running)
                    return;
                const auto now = Clock::now();
                paused = !paused;
                if (paused)
                    pauseStarted = now;
                else
                    frameCounter.excludePause(now - pauseStarted);
            };
            emulationCallbacks.isPaused = [&paused]()
            { return paused; };
            emulationCallbacks.fps = [&frameCounter]()
            { return frameCounter.fps(); };
            Window::Window window([&cartridge, &board, &running, &paused, &lastError, &lastBusAccess, &firstCpuException, &frameCounter](const std::filesystem::path &path)
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
                frameCounter.reset(Clock::now());
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
            enum class LoopMode
            {
                Native,
                VSync,
                FastForward
            };
            constexpr Uint64 masterClock = 53693175;
            constexpr unsigned cyclesPerFrame = Core::VDP::masterCyclesPerLine * Core::VDP::linesPerFrame;
            constexpr Uint64 framePeriodNs = cyclesPerFrame * SDL_NS_PER_SECOND / masterClock;
            constexpr unsigned fastFramesPerPresent = 4;
            auto mode = LoopMode::Native;
            auto deadline = SDL_GetTicksNS();
            bool wasEmulating = false;
            unsigned fastFrames = 0;
            while (window.processEvents())
            {
                const auto buttons = input.update(window.acceptsInput());
                for (unsigned player = 0; player < buttons.size(); ++player)
                    board.setControllerButtons(player, buttons[player]);
                const bool emulating = running && !paused;
                const bool fastForward = emulating && window.fastForwardEnabled() && input.fastForwardPressed();
                const bool vsync = window.synchronizeVSync(fastForward);
                const auto nextMode = fastForward ? LoopMode::FastForward
                                      : vsync     ? LoopMode::VSync
                                                  : LoopMode::Native;
                if (nextMode == LoopMode::Native)
                {
                    const auto now = SDL_GetTicksNS();
                    // Trocas de modo e pausas longas iniciam um novo prazo.
                    if (mode != nextMode || emulating != wasEmulating ||
                        (emulating && board.masterCycles() == 0) || now > deadline + 4 * framePeriodNs)
                        deadline = now;
                    deadline += framePeriodNs;
                }
                mode = nextMode;
                wasEmulating = emulating;
                if (emulating)
                {
                    const auto previousFrame = board.vdp().frame();
                    for (unsigned cycle = 0; cycle < cyclesPerFrame; ++cycle)
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
                    if (board.vdp().frame() != previousFrame)
                        frameCounter.record(Clock::now());
                }
                // Entre quadros acelerados, volta ao processamento de eventos para ler a soltura de Tab.
                if (running && mode == LoopMode::FastForward && ++fastFrames < fastFramesPerPresent)
                    continue;
                fastFrames = 0;
                window.render();
                if (mode == LoopMode::Native)
                {
                    const auto now = SDL_GetTicksNS();
                    if (now < deadline)
                        SDL_DelayPrecise(deadline - now);
                }
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