#include "Systems/Engine.h"
#include "Window/Window.h"
#include "Core/Cartridge/Cartridge.h"
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
            Window::Window window([&cartridge](const std::filesystem::path &path)
                                  {
                auto loaded = Core::Cartridge::load(path);
                auto name = loaded.romName();
                cartridge = std::move(loaded);
                return name; }, [&cartridge]() -> const std::vector<std::uint8_t> *
                                  { return cartridge ? &cartridge->rom() : nullptr; });
            window.open();
            while (window.processEvents())
                window.render();
            return 0;
        }
        catch (const std::exception &error)
        {
            std::cerr << "R2MEGA: " << error.what() << '\n';
            return 1;
        }
    }
}