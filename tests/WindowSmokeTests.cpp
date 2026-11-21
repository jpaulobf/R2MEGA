#include "Window/Window.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <filesystem>
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    void require(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void activateMenu(R2MEGA::Window::Window &window, const char *item, const char *menuName = "File")
    {
        window.render();
        auto *bar = ImGui::FindWindowByName("##MainMenuBar");
        require(bar != nullptr, "Menu bar is missing");
        const auto menuBar = ImHashStr("##MenuBar", 0, bar->ID);
        ImGui::ActivateItemByID(ImHashStr(menuName, 0, menuBar));
        window.render();
        auto *menu = ImGui::FindWindowByName("###Menu_00");
        require(menu && menu->Active, "Menu did not open");
        ImGui::ActivateItemByID(ImHashStr(item, 0, menu->ID));
        window.render();
    }

    void testAbout()
    {
        R2MEGA::Window::Window window;
        window.open();
        require(window.processEvents(), "Window should be running");
        activateMenu(window, "About R2MEGA", "About");
        auto *popup = ImGui::FindWindowByName("About R2MEGA");
        require(popup && popup->Active, "About menu must show the dialog");
        ImGui::ActivateItemByID(ImHashStr("Close", 0, popup->ID));
        window.render();
        window.render();
        require(!popup->Active, "Close must dismiss the About dialog");
        activateMenu(window, "About R2MEGA", "About");
        require(popup->Active, "About dialog must reopen");
    }

    void testRomSelection()
    {
        const auto previous = std::filesystem::current_path();
        const auto fixtures = previous / "window-test-fixtures";
        std::filesystem::create_directories(fixtures);
        std::ofstream(fixtures / "game.bin", std::ios::binary).put('R');
        std::filesystem::current_path(fixtures);
        try
        {
            int attempts = 0;
            R2MEGA::Window::Window window([&](const std::filesystem::path &path)
                                          {
                ++attempts;
                require(path.filename() == "game.bin", "Unexpected selected file");
                if (attempts == 1) throw std::runtime_error("Test load failure");
                return std::string("game.bin"); });
            window.open();
            window.processEvents();
            activateMenu(window, "Open...");
            auto selectRom = [&]
            {
                ImGuiWindow *files = nullptr;
                for (auto *candidate : ImGui::GetCurrentContext()->Windows)
                    if (std::string(candidate->Name).find("Open ROM/Files_") == 0 && candidate->Active)
                        files = candidate;
                require(files != nullptr, "File list is missing");
                const int index = 0;
                const auto entry = ImHashData(&index, sizeof(index), files->ID);
                ImGui::ActivateItemByID(ImHashStr("##entry", 0, entry));
                window.render();
            };
            selectRom();
            require(attempts == 1 && ImGui::FindWindowByName("Open ROM")->Active,
                    "Failed loading must leave the browser open");
            selectRom();
            require(attempts == 2, "Selecting a ROM must invoke the loading callback");
            window.render();
            require(!ImGui::FindWindowByName("Open ROM")->Active, "Successful loading must close the browser");
            int count = 0;
            SDL_Window **windows = SDL_GetWindows(&count);
            require(windows && count == 1, "Expected one SDL window");
            const std::string title = SDL_GetWindowTitle(windows[0]);
            SDL_free(windows);
            require(title.find("game.bin") != std::string::npos, "Loaded ROM name must appear in the title");
        }
        catch (...)
        {
            std::filesystem::current_path(previous);
            throw;
        }
        std::filesystem::current_path(previous);
    }

    void testOpenAndExit()
    {
        R2MEGA::Window::Window window;
        window.open();
        require(window.processEvents(), "Window should be running");
        activateMenu(window, "Open...");
        auto *popup = ImGui::FindWindowByName("Open ROM");
        require(popup && popup->Active, "Open menu must show the ROM browser");
        ImGui::ClosePopupsOverWindow(nullptr, false);
        window.render();
        activateMenu(window, "Exit");
        require(!window.processEvents(), "Exit must stop the window");
    }

    void testVideoOutput()
    {
        std::array<std::uint32_t, 320 * 224> pixels{};
        pixels.fill(0xff123456);
        int reads = 0;
        R2MEGA::Window::Window window({}, [&]()
                                      {
                                          ++reads;
                                          return pixels.data(); });
        window.open();
        window.render();
        require(reads == 1, "Main window must request the VDP framebuffer");
        auto *video = ImGui::FindWindowByName("Saída da VDP");
        require(!video || !video->Active, "VDP must not open a secondary window");
    }

    void testShortcutAndClose()
    {
        R2MEGA::Window::Window window;
        window.open();
        window.processEvents();
        window.render();
        SDL_Window *native = nullptr;
        // Os identificadores de janela não são reutilizados entre sessões SDL; busca a janela ativa.
        int count = 0;
        SDL_Window **windows = SDL_GetWindows(&count);
        require(windows && count == 1, "Expected one SDL window");
        native = windows[0];
        SDL_free(windows);
        SDL_Event key{};
        key.type = SDL_EVENT_KEY_DOWN;
        key.key.windowID = SDL_GetWindowID(native) + 1000;
        key.key.key = SDLK_O;
        key.key.mod = SDL_KMOD_CTRL;
        require(SDL_PushEvent(&key), "Could not queue shortcut");
        window.processEvents();
        window.render();
        auto *popup = ImGui::FindWindowByName("Open ROM");
        require(!popup || !popup->Active, "Shortcut from another window must be ignored");
        key.key.windowID = SDL_GetWindowID(native);
        require(SDL_PushEvent(&key), "Could not queue shortcut");
        window.processEvents();
        window.render();
        popup = ImGui::FindWindowByName("Open ROM");
        require(popup && popup->Active, "Ctrl+O must show the ROM browser");
        SDL_Event close{};
        close.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
        close.window.windowID = SDL_GetWindowID(native);
        require(SDL_PushEvent(&close), "Could not queue close event");
        require(!window.processEvents(), "Window close must stop the application");
    }
}

int main()
{
    try
    {
        testOpenAndExit();
        testShortcutAndClose();
        testAbout();
        testRomSelection();
        testVideoOutput();
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}