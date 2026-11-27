#include "Window/Window.h"
#include "Systems/FrameCounter.h"
#include "Systems/Input.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <filesystem>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace
{
    void require(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    ImGuiWindow *openMenu(R2MEGA::Window::Window &window, const char *menuName)
    {
        window.render();
        auto *bar = ImGui::FindWindowByName("##MainMenuBar");
        require(bar != nullptr, "Menu bar is missing");
        const auto menuBar = ImHashStr("##MenuBar", 0, bar->ID);
        ImGui::ActivateItemByID(ImHashStr(menuName, 0, menuBar));
        window.render();
        auto *menu = ImGui::FindWindowByName("###Menu_00");
        require(menu && menu->Active, "Menu did not open");
        return menu;
    }

    void activateMenu(R2MEGA::Window::Window &window, const char *item, const char *menuName = "File")
    {
        auto *menu = openMenu(window, menuName);
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
        const auto games = fixtures / std::filesystem::u8path(u8"sele\u00e7\u00e3o");
        const auto recentGames = fixtures / "recent=games";
        const auto game = (games / "game.bin").make_preferred();
        const auto config = fixtures / "resources/config.ini";
        const auto oldGame = [&](int number)
        { return (recentGames / ("old" + std::to_string(number) + ".zip")).make_preferred(); };
        const auto readConfig = [&]
        {
            std::ifstream input(config, std::ios::binary);
            require(static_cast<bool>(input), "Could not read the ROM preferences");
            return std::string(std::istreambuf_iterator<char>(input), {});
        };
        std::filesystem::create_directories(games);
        std::filesystem::create_directories(recentGames);
        std::filesystem::create_directories(config.parent_path());
        std::ofstream(game, std::ios::binary).put('R');
        std::ofstream(oldGame(1), std::ios::binary).put('R');
        {
            std::ofstream output(config, std::ios::binary);
            output << "keep_setting=yes\r\nlast_rom_path=" << games.u8string() << "\r\n";
            for (int i = 1; i <= 10; ++i)
                output << 'f' << i << '=' << oldGame(i).u8string() << "\r\n";
        }
        const auto originalConfig = readConfig();
        std::filesystem::current_path(fixtures);
        try
        {
            int attempts = 0, resets = 0, unloads = 0, videoReads = 0;
            bool loaded = false;
            std::array<std::uint32_t, 320 * 224> pixels{};
            using namespace std::chrono;
            using Counter = R2MEGA::Systems::FrameCounter;
            const auto start = Counter::Clock::time_point{};
            Counter counter(start);
            R2MEGA::Window::Window::EmulationCallbacks emulation;
            emulation.fps = [&counter]()
            { return counter.fps(); };
            emulation.hasRom = [&loaded]()
            { return loaded; };
            emulation.reset = [&]()
            {
                ++resets;
                counter.reset(start);
            };
            emulation.unload = [&]()
            {
                ++unloads;
                loaded = false;
                counter.reset(start);
            };
            R2MEGA::Window::Window window([&](const std::filesystem::path &path)
                                          {
                ++attempts;
                require(path == game || path == oldGame(1), "ROM is unavailable");
                if (attempts == 1) throw std::runtime_error("Test load failure");
                loaded = true;
                counter.reset(start);
                return path.filename().u8string(); },
                                          [&]()
                                          { return loaded ? pixels.data() : nullptr; },
                                          {}, [&]()
                                          {
                ++videoReads;
                return R2MEGA::Window::Window::VideoStatus{}; }, {}, std::move(emulation));
            window.open();
            window.processEvents();
            int count = 0;
            SDL_Window **windows = SDL_GetWindows(&count);
            require(windows && count == 1, "Expected one SDL window");
            auto *native = windows[0];
            SDL_free(windows);
            const auto title = [native]()
            { return std::string(SDL_GetWindowTitle(native)); };
            const auto idleTitle = title();
            require(idleTitle.find(" | FPS: 0.00") != std::string::npos, "Idle title must report zero FPS");
            const auto id = SDL_GetWindowID(native);
            const auto pressReset = [&](SDL_WindowID target, bool repeat = false, SDL_Keymod mod = SDL_KMOD_NONE)
            {
                SDL_Event key{};
                key.type = SDL_EVENT_KEY_DOWN;
                key.key.windowID = target;
                key.key.key = SDLK_F12;
                key.key.scancode = SDL_SCANCODE_F12;
                key.key.repeat = repeat;
                key.key.mod = mod;
                require(SDL_PushEvent(&key) && window.processEvents(), "Could not process F12");
            };
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
            require(title().find("game.bin") == std::string::npos, "Failed loading must leave the title intact");
            require(readConfig() == originalConfig, "Failed loading must leave the ROM preferences intact");
            selectRom();
            require(attempts == 2, "Selecting a ROM must invoke the loading callback");
            window.render();
            require(!ImGui::FindWindowByName("Open ROM")->Active, "Successful loading must close the browser");
            require(title().find(" | FPS: 0.00 | game.bin") != std::string::npos,
                    "Loaded ROM name must appear after the reset FPS counter");
            const auto updatedConfig = readConfig();
            require(updatedConfig.find("f1=" + game.u8string() + '\n') != std::string::npos &&
                        updatedConfig.find("f10=" + oldGame(9).u8string() + '\n') != std::string::npos &&
                        updatedConfig.find(oldGame(10).u8string()) == std::string::npos &&
                        updatedConfig.find("keep_setting=yes\r\n") != std::string::npos,
                    "Config must preserve other settings and retain the ten most recent ROMs");

            for (unsigned frame = 1; frame <= 30; ++frame)
                counter.record(start + nanoseconds(frame * 500000000ull / 30));
            window.render();
            require(title().find(" | FPS: 60.00 | game.bin") != std::string::npos,
                    "Native title must show the measured FPS before the ROM name");
            for (unsigned frame = 1; frame <= 120; ++frame)
                counter.record(start + milliseconds(500) + nanoseconds(frame * 500000000ull / 120));
            window.render();
            require(title().find(" | FPS: 240.00 | game.bin") != std::string::npos,
                    "FPS must count all emulated frames, including frames between presentations");
            counter.excludePause(seconds(5));
            window.render();
            require(title().find(" | FPS: 240.00 | game.bin") != std::string::npos,
                    "The title must preserve the last FPS while paused");
            counter.record(start + seconds(6) + milliseconds(5));
            window.render();
            require(title().find(" | FPS: 200.00 | game.bin") != std::string::npos,
                    "The next sample must exclude time spent paused");

            const auto selectRecent = [&](const char *name, int index)
            {
                auto *menu = openMenu(window, "File");
                ImGui::ActivateItemByID(ImHashStr("Recent Files", 0, menu->ID));
                window.render();
                auto *recent = ImGui::FindWindowByName("###Menu_01");
                require(recent && recent->Active, "Recent Files submenu did not open");
                ImGui::ActivateItemByID(ImHashStr(name, 0, ImHashData(&index, sizeof(index), recent->ID)));
                window.render();
                window.render();
            };
            selectRecent("old1.zip", 1);
            require(attempts == 3 && title().find(" | old1.zip") != std::string::npos &&
                        !ImGui::FindWindowByName("Open ROM")->Active,
                    "Recent Files must reopen a ROM directly and close the browser");
            const auto reorderedConfig = readConfig();
            require(reorderedConfig.find("f1=" + oldGame(1).u8string() + "\nf2=" + game.u8string() +
                                         "\nf3=" + oldGame(2).u8string() + '\n') != std::string::npos &&
                        reorderedConfig.find("last_rom_path=" + oldGame(1).parent_path().u8string() + '\n') != std::string::npos,
                    "Reopening a ROM must move it to the top without duplicates and update the last directory");

            selectRecent("old2.zip", 2);
            require(attempts == 4 && ImGui::FindWindowByName("Open ROM")->Active &&
                        readConfig() == reorderedConfig,
                    "Unavailable recent ROMs must leave the browser open and keep the history intact");
            pressReset(id);
            require(resets == 0, "F12 must be ignored while the ROM browser is open");
            selectRom();
            require(attempts == 5 && title().find(" | old1.zip") != std::string::npos,
                    "The browser must return to the last successful directory");

            activateMenu(window, "Reset");
            require(resets == 1 && loaded && attempts == 5, "File > Reset must restart the loaded ROM");
            pressReset(id + 1000);
            pressReset(id, true);
            pressReset(id, false, SDL_KMOD_CTRL);
            pressReset(id);
            require(resets == 2 && readConfig() == reorderedConfig,
                    "Only plain F12 in this window must reset without changing the ROM history");

            activateMenu(window, "Unload");
            require(unloads == 1 && !loaded && title() == idleTitle, "Unload must restore the idle window");
            const auto previousVideoReads = videoReads;
            pressReset(id);
            auto *fileMenu = openMenu(window, "File");
            ImGui::ActivateItemByID(ImHashStr("Reset", 0, fileMenu->ID));
            window.render();
            ImGui::ActivateItemByID(ImHashStr("Unload", 0, fileMenu->ID));
            window.render();
            require(resets == 2 && unloads == 1 && videoReads == previousVideoReads &&
                        readConfig() == reorderedConfig,
                    "Idle window must disable Reset and Unload, stop drawing video, and keep the ROM history");
            ImGui::ClosePopupsOverWindow(nullptr, false);
            window.render();
            selectRecent("old1.zip", 0);
            require(loaded && attempts == 6, "Recent Files must remain available after unloading a ROM");
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
        using Window = R2MEGA::Window::Window;
        Window::EmulationCallbacks emulation;
        emulation.isPaused = []()
        { return true; };
        R2MEGA::Window::Window window({}, [&]()
                                      {
                                          ++reads;
                                          return pixels.data(); }, {}, []()
                                      { return Window::VideoStatus{}; }, {}, std::move(emulation));
        window.open();
        window.processEvents();
        window.render();
        require(reads == 1, "Main window must request the VDP framebuffer");
        auto *video = ImGui::FindWindowByName("Saída da VDP");
        require(!video || !video->Active, "VDP must not open a secondary window");
        const auto debugDrawn = []()
        { return ImGui::GetBackgroundDrawList()->VtxBuffer.Size > 0; };
        require(!debugDrawn(), "Debug overlay must be hidden by default");

        int count = 0;
        SDL_Window **windows = SDL_GetWindows(&count);
        require(windows && count == 1, "Expected one SDL window for the debug shortcut");
        const auto id = SDL_GetWindowID(windows[0]);
        SDL_free(windows);
        SDL_Event key{};
        key.type = SDL_EVENT_KEY_DOWN;
        key.key.key = SDLK_F1;
        const auto press = [&](SDL_WindowID windowId, bool repeat = false)
        {
            key.key.windowID = windowId;
            key.key.repeat = repeat;
            require(SDL_PushEvent(&key) && window.processEvents(), "Could not process F1");
            window.render();
        };
        press(id);
        require(debugDrawn(), "F1 must show the debug overlay even while paused");
        press(id, true);
        require(debugDrawn(), "Key repeat must not toggle the debug overlay");
        press(id + 1000);
        require(debugDrawn(), "F1 from another window must not toggle the overlay");
        press(id);
        require(!debugDrawn(), "A second F1 press must hide the debug overlay");
    }

    void testInputMenu()
    {
        bool joystick[2]{};
        R2MEGA::Window::Window::InputCallbacks input;
        input.playerUsesJoystick = [&](unsigned player)
        { return joystick[player]; };
        input.setPlayerJoystick = [&](unsigned player, bool enabled)
        { joystick[player] = enabled; };
        R2MEGA::Window::Window window({}, {}, {}, {}, std::move(input));
        window.open();
        window.processEvents();
        activateMenu(window, "Configure Keyboard...", "Input");
        auto *popup = ImGui::FindWindowByName("Configure Keyboard");
        require(popup && popup->Active, "Input menu must open the keyboard dialog");
        require(!window.acceptsInput(), "Keyboard dialog must block game input");
        ImGui::ActivateItemByID(popup->GetID("Cancel"));
        window.render();
        window.render();
        require(!popup->Active, "Cancel must close the keyboard dialog");
    }

    void testKeyboardToWindow()
    {
        R2MEGA::Systems::Input input;
        R2MEGA::Window::Window::InputCallbacks callbacks;
        callbacks.event = [&](const SDL_Event &event, bool enabled)
        { input.handleEvent(event, enabled); };
        callbacks.setKeyboardMapping = [&](const R2MEGA::Systems::KeyboardMapping &mapping)
        { input.setKeyboardMapping(mapping); };
        R2MEGA::Window::Window window({}, {}, {}, {}, std::move(callbacks));
        window.open();
        window.render();
        int count = 0;
        SDL_Window **windows = SDL_GetWindows(&count);
        require(windows && count == 1, "Expected one SDL window for input");
        const auto id = SDL_GetWindowID(windows[0]);
        SDL_free(windows);
        SDL_Event focus{};
        focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
        focus.window.windowID = id;
        require(SDL_PushEvent(&focus), "Could not queue focus event");
        SDL_Event key{};
        key.type = SDL_EVENT_KEY_DOWN;
        key.key.windowID = id;
        key.key.key = SDLK_J;
        require(SDL_PushEvent(&key), "Could not queue player key");
        window.processEvents();
        require(input.update(window.acceptsInput())[0] == R2MEGA::Core::ControllerInput::A,
                "Window must pass the configured key to player one");
        focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        require(SDL_PushEvent(&focus), "Could not queue focus loss");
        window.processEvents();
        require(input.update(window.acceptsInput())[0] == 0,
                "Focus loss must release the game button");
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

    void testPauseShortcut()
    {
        bool running = false;
        bool paused = false;
        R2MEGA::Window::Window::EmulationCallbacks emulation;
        emulation.togglePause = [&]()
        { if (running) paused = !paused; };
        emulation.isPaused = [&]()
        { return paused; };
        R2MEGA::Window::Window window({}, {}, [&]()
                                      { return running; }, {}, {}, std::move(emulation));
        window.open();
        window.render();
        int count = 0;
        SDL_Window **windows = SDL_GetWindows(&count);
        require(windows && count == 1, "Expected one SDL window for pause");
        const auto id = SDL_GetWindowID(windows[0]);
        SDL_free(windows);

        SDL_Event focus{};
        focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
        focus.window.windowID = id;
        require(SDL_PushEvent(&focus), "Could not queue focus event");
        SDL_Event key{};
        key.type = SDL_EVENT_KEY_DOWN;
        key.key.windowID = id;
        key.key.key = SDLK_PAUSE;
        require(SDL_PushEvent(&key), "Could not queue pause key");
        window.processEvents();
        require(!paused, "Pause must be ignored before a ROM starts");

        running = true;
        require(SDL_PushEvent(&key), "Could not queue pause key");
        window.processEvents();
        require(paused, "Pause/Break must pause emulation");
        require(!window.acceptsInput(), "Pause must block game input");

        key.key.repeat = true;
        require(SDL_PushEvent(&key), "Could not queue repeated pause key");
        window.processEvents();
        require(paused, "Key repeat must not resume emulation");

        key.key.repeat = false;
        key.key.windowID = id + 1000;
        require(SDL_PushEvent(&key), "Could not queue foreign pause key");
        window.processEvents();
        require(paused, "Pause from another window must be ignored");

        key.key.windowID = id;
        require(SDL_PushEvent(&key), "Could not queue resume key");
        window.processEvents();
        require(!paused, "Pause/Break must resume emulation");
        require(window.acceptsInput(), "Resuming must allow game input");
    }

    void testSpeedControls()
    {
        R2MEGA::Systems::Input input;
        R2MEGA::Window::Window::InputCallbacks callbacks;
        callbacks.event = [&](const SDL_Event &event, bool enabled)
        { input.handleEvent(event, enabled); };
        R2MEGA::Window::Window window({}, {}, {}, {}, std::move(callbacks));
        window.open();
        window.processEvents();

        int count = 0;
        SDL_Window **windows = SDL_GetWindows(&count);
        require(windows && count == 1, "Expected one SDL window for speed controls");
        const auto id = SDL_GetWindowID(windows[0]);
        auto *renderer = SDL_GetRenderer(windows[0]);
        SDL_free(windows);
        require(renderer != nullptr, "Speed controls require an SDL renderer");
        require(window.fastForwardEnabled(), "Fast-forward must be enabled by default");
        const auto checkMode = [&](const char *scenario, bool fastExpected, int vsyncExpected)
        {
            input.update(window.acceptsInput());
            const bool fast = window.fastForwardEnabled() && input.fastForwardPressed();
            int vsync = 0;
            if (fast != fastExpected || window.synchronizeVSync(fast) != (vsyncExpected == 1) ||
                !SDL_GetRenderVSync(renderer, &vsync) || vsync != vsyncExpected)
                throw std::runtime_error(std::string("Unexpected speed mode for ") + scenario);
        };
        const auto focus = [&](SDL_EventType type)
        {
            SDL_Event event{};
            event.type = type;
            event.window.windowID = id;
            require(SDL_PushEvent(&event) && window.processEvents(), "Could not process focus event");
        };
        const auto tab = [&](SDL_EventType type, SDL_Keymod mod = SDL_KMOD_NONE)
        {
            SDL_Event event{};
            event.type = type;
            event.key.windowID = id;
            event.key.key = SDLK_TAB;
            event.key.scancode = SDL_SCANCODE_TAB;
            event.key.mod = mod;
            require(SDL_PushEvent(&event) && window.processEvents(), "Could not process Tab event");
        };

        focus(SDL_EVENT_WINDOW_FOCUS_GAINED);
        checkMode("default settings", false, 1);
        tab(SDL_EVENT_KEY_DOWN, SDL_KMOD_ALT);
        checkMode("Alt+Tab", false, 1);
        tab(SDL_EVENT_KEY_UP);
        tab(SDL_EVENT_KEY_DOWN);
        checkMode("Tab held with VSync enabled", true, 0);
        window.render();
        window.processEvents();
        checkMode("Tab still held after presenting a frame", true, 0);
        tab(SDL_EVENT_KEY_UP);
        checkMode("Tab released with VSync enabled", false, 1);

        activateMenu(window, "Enable Fast-Forward", "Hacks");
        require(!window.fastForwardEnabled(), "Hacks must toggle fast-forward");
        window.render();
        tab(SDL_EVENT_KEY_DOWN);
        checkMode("Tab held with fast-forward disabled", false, 1);
        tab(SDL_EVENT_KEY_UP);

        activateMenu(window, "VSync", "Display");
        checkMode("VSync unchecked", false, 0);
        activateMenu(window, "Enable Fast-Forward", "Hacks");
        require(window.fastForwardEnabled(), "Hacks must re-enable fast-forward");
        window.render();
        tab(SDL_EVENT_KEY_DOWN);
        checkMode("Tab held with VSync unchecked", true, 0);
        tab(SDL_EVENT_KEY_UP);
        checkMode("Tab released with VSync unchecked", false, 0);

        activateMenu(window, "VSync", "Display");
        window.render();
        tab(SDL_EVENT_KEY_DOWN);
        checkMode("Tab held after checking VSync again", true, 0);
        focus(SDL_EVENT_WINDOW_FOCUS_LOST);
        checkMode("focus lost while holding Tab", false, 1);
        focus(SDL_EVENT_WINDOW_FOCUS_GAINED);
        checkMode("focus restored", false, 1);
    }
}

int main()
{
    try
    {
        testOpenAndExit();
        testShortcutAndClose();
        testPauseShortcut();
        testAbout();
        testRomSelection();
        testVideoOutput();
        testInputMenu();
        testKeyboardToWindow();
        testSpeedControls();
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}