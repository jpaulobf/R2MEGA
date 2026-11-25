#include "Systems/Engine.h"
#include "Systems/FrameCounter.h"
#include "Systems/FramePacer.h"
#include "Systems/Input.h"
#include "Window/Window.h"
#include "Core/Cartridge/Cartridge.h"
#include "Core/Board.h"
#include <algorithm>
#include <exception>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string_view>

namespace R2MEGA::Systems
{
    namespace
    {
        void reportLoadedCartridge(const Core::Cartridge &cartridge)
        {
            const auto singleLine = [](std::string value)
            {
                std::replace_if(value.begin(), value.end(), [](unsigned char character)
                                { return character < 0x20 || character == 0x7f; }, '?');
                return value;
            };
            std::ostringstream report;
            report << "System: Mega Drive / Genesis (NTSC)\n"
                   << "File: " << singleLine(cartridge.path().u8string()) << '\n';
            if (cartridge.romName() != cartridge.path().filename().u8string())
                report << "ZIP entry: " << singleLine(cartridge.romName()) << '\n';
            report << "ROM: " << cartridge.rom().size() << " bytes"
                   << " | System RAM: " << Core::WorkRAM::size() << " bytes"
                   << " | Z80 RAM: " << Core::AudioRAM::size() << " bytes"
                   << " | VRAM: " << Core::VRAM::size() << " bytes\n";

            if (const auto header = cartridge.header())
            {
                const auto orUnknown = [](std::string_view text)
                { return text.empty() ? std::string_view("unknown") : text; };
                const auto &title = header->overseasTitle.empty() ? header->domesticTitle : header->overseasTitle;
                report << "Sega header: 0x" << std::hex << std::uppercase << std::setfill('0')
                       << std::setw(4) << Core::Cartridge::Header::offset
                       << " | Region: " << orUnknown(header->region)
                       << " | Header checksum: 0x" << std::setw(4) << header->checksum << std::dec << '\n'
                       << "Title: " << orUnknown(title) << " | Product: " << orUnknown(header->productCode)
                       << " | Console ID: " << header->console << '\n';
            }
            else
                report << "Sega header: unavailable\n";

            report << "Video: Mode 5, H32/H40, " << Core::VDP::visibleLines
                   << " lines | Controllers: 3-button\n";
            std::cout << report.str() << std::flush;
        }
    }

    int Engine::run()
    {
        try
        {
            std::optional<Core::Cartridge> cartridge;
            Core::Board board;
            Input input;
            bool running = false;
            bool paused = false;
            bool resetTiming = true;
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
                resetTiming = true;
                if (paused)
                    pauseStarted = now;
                else
                    frameCounter.excludePause(now - pauseStarted);
            };
            emulationCallbacks.isPaused = [&paused]()
            { return paused; };
            emulationCallbacks.fps = [&frameCounter]()
            { return frameCounter.fps(); };
            Window::Window window([&cartridge, &board, &running, &paused, &resetTiming, &lastError, &lastBusAccess, &firstCpuException, &frameCounter](const std::filesystem::path &path)
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
                resetTiming = true;
                reportLoadedCartridge(*cartridge);
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
            constexpr unsigned masterClock = 53693175;
            constexpr unsigned cyclesPerFrame = Core::VDP::masterCyclesPerLine * Core::VDP::linesPerFrame;
            constexpr unsigned fastFramesPerPresent = 4;
            FramePacer pacer(masterClock, cyclesPerFrame);
            bool wasFastForward = false;
            unsigned fastFrames = 0;
            while (window.processEvents())
            {
                const auto buttons = input.update(window.acceptsInput());
                for (unsigned player = 0; player < buttons.size(); ++player)
                    board.setControllerButtons(player, buttons[player]);
                const bool emulating = running && !paused;
                const bool fastForward = emulating && window.fastForwardEnabled() && input.fastForwardPressed();
                const bool vsync = window.synchronizeVSync(fastForward);
                const auto now = SDL_GetTicksNS();
                if (resetTiming || fastForward != wasFastForward)
                {
                    pacer.reset(now);
                    resetTiming = false;
                }
                wasFastForward = fastForward;
                const unsigned framesPerIteration = fastForward ? 1 : FramePacer::maxFramesPerPresent;
                // O clock da placa determina quais quadros estão prontos para emular.
                for (unsigned frame = 0; emulating && running && frame < framesPerIteration &&
                                         (fastForward || pacer.takeFrame(now));
                     ++frame)
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
                if (running && fastForward && ++fastFrames < fastFramesPerPresent)
                    continue;
                fastFrames = 0;
                window.render();
                if (!vsync && !fastForward)
                {
                    const auto afterRender = SDL_GetTicksNS();
                    if (!emulating)
                        pacer.takeFrame(afterRender);
                    if (afterRender < pacer.deadline())
                        SDL_DelayPrecise(pacer.deadline() - afterRender);
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