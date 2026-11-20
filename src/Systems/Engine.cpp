#include "Systems/Engine.h"
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
            bool running = false;
            Window::Window window([&cartridge, &board, &running](const std::filesystem::path &path)
                                  {
                auto loaded = Core::Cartridge::load(path);
                auto name = loaded.romName();
                cartridge = std::move(loaded);
                board.insertCartridge(*cartridge);
                running = true;
                return name; }, [&cartridge]() -> const std::vector<std::uint8_t> *
                                  { return cartridge ? &cartridge->rom() : nullptr; },
                                  [&cartridge, &board]() -> const std::uint32_t *
                                  { return cartridge ? board.vdp().pixels().data() : nullptr; },
                                  [&running]()
                                  { return running; });
            window.open();
            while (window.processEvents())
            {
                if (running)
                    for (unsigned i = 0; i < 50000; ++i)
                    {
                        try
                        {
                            board.clock();
                        }
                        catch (const std::logic_error &)
                        {
                            running = false;
                            break;
                        }
                    }
                window.render();
            }
            return 0;
        }
        catch (const std::exception &error)
        {
            std::cerr << "R2MEGA: " << error.what() << '\n';
            return 1;
        }
    }
}