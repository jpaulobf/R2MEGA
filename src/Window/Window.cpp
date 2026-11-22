#include "Window/Window.h"
#include "Window/FileManager.h"
#include "Window/KeyboardConfig.h"
#include "Window/KeyboardDialog.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace R2MEGA::Window
{
    struct Window::Impl
    {
        SDL_Window *window = nullptr;
        SDL_Renderer *renderer = nullptr;
        SDL_Texture *videoTexture = nullptr;
        bool sdlReady = false, contextReady = false, platformReady = false, rendererReady = false;
        bool running = true;
        bool aboutOpen = false;
        OpenHandler openHandler;
        VideoProvider videoProvider;
        RunningProvider runningProvider;
        VideoStatusProvider videoStatusProvider;
        InputCallbacks inputCallbacks;
        FileManager files;
        KeyboardConfig keyboardConfig;
        KeyboardDialog keyboardDialog;
        bool focused = false, minimized = false;
        explicit Impl(OpenHandler handler, VideoProvider video,
                      RunningProvider running, VideoStatusProvider status, InputCallbacks input)
            : openHandler(std::move(handler)), videoProvider(std::move(video)),
              runningProvider(std::move(running)), videoStatusProvider(std::move(status)),
              inputCallbacks(std::move(input)),
              files([this](const std::filesystem::path &path)
                    {
                if (!openHandler) throw std::runtime_error("ROM loading is unavailable.");
                const auto name = openHandler(path);
                const auto title = std::string("R2MEGA v." R2_APP_VERSION " | ") + name;
                SDL_SetWindowTitle(window, title.c_str()); }) {}
        ~Impl()
        {
            if (rendererReady)
                ImGui_ImplSDLRenderer3_Shutdown();
            if (platformReady)
                ImGui_ImplSDL3_Shutdown();
            if (contextReady)
                ImGui::DestroyContext();
            if (videoTexture)
                SDL_DestroyTexture(videoTexture);
            if (renderer)
                SDL_DestroyRenderer(renderer);
            if (window)
                SDL_DestroyWindow(window);
            if (sdlReady)
                SDL_Quit();
        }
    };

    Window::Window(OpenHandler openHandler, VideoProvider videoProvider,
                   RunningProvider runningProvider, VideoStatusProvider videoStatusProvider,
                   InputCallbacks inputCallbacks)
        : impl_(std::make_unique<Impl>(std::move(openHandler), std::move(videoProvider), std::move(runningProvider),
                                       std::move(videoStatusProvider), std::move(inputCallbacks))) {}
    Window::~Window() = default;

    void Window::open()
    {
        auto &s = *impl_;
        if (s.sdlReady)
            throw std::logic_error("Window session has already been initialized.");
        if (!SDL_Init(SDL_INIT_VIDEO))
            throw std::runtime_error(SDL_GetError());
        s.sdlReady = true;
        s.keyboardDialog.setMapping(s.keyboardConfig.load());
        if (s.inputCallbacks.setKeyboardMapping)
            s.inputCallbacks.setKeyboardMapping(s.keyboardDialog.mapping());
        s.window = SDL_CreateWindow("R2MEGA v." R2_APP_VERSION, 960, 672,
                                    SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!s.window)
            throw std::runtime_error(SDL_GetError());
        s.focused = (SDL_GetWindowFlags(s.window) & SDL_WINDOW_INPUT_FOCUS) != 0;
        s.renderer = SDL_CreateRenderer(s.window, nullptr);
        if (!s.renderer)
            throw std::runtime_error(SDL_GetError());
        if (s.videoProvider)
        {
            s.videoTexture = SDL_CreateTexture(s.renderer, SDL_PIXELFORMAT_ARGB8888,
                                               SDL_TEXTUREACCESS_STREAMING, 320, 224);
            if (!s.videoTexture)
                throw std::runtime_error(SDL_GetError());
            SDL_SetTextureScaleMode(s.videoTexture, SDL_SCALEMODE_NEAREST);
        }
        if (!SDL_SetRenderVSync(s.renderer, 1))
            throw std::runtime_error(SDL_GetError());
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        s.contextReady = true;
        auto &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigNavCaptureKeyboard = false;
        io.IniFilename = nullptr;
        ImGui::StyleColorsDark();
        const float scale = std::max(1.0f, SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()));
        ImGui::GetStyle().ScaleAllSizes(scale);
        ImGui::GetStyle().FontScaleDpi = scale;
        if (!ImGui_ImplSDL3_InitForSDLRenderer(s.window, s.renderer))
            throw std::runtime_error("Unable to initialize the ImGui SDL3 backend.");
        s.platformReady = true;
        if (!ImGui_ImplSDLRenderer3_Init(s.renderer))
            throw std::runtime_error("Unable to initialize the ImGui renderer backend.");
        s.rendererReady = true;
    }

    bool Window::processEvents()
    {
        auto &s = *impl_;
        if (!s.rendererReady)
            throw std::logic_error("Open the window before processing events.");
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (s.keyboardDialog.processEvent(event, SDL_GetWindowID(s.window)))
                continue;
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED && event.window.windowID == SDL_GetWindowID(s.window))
                s.focused = true;
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST && event.window.windowID == SDL_GetWindowID(s.window))
                s.focused = false;
            if (event.type == SDL_EVENT_WINDOW_MINIMIZED && event.window.windowID == SDL_GetWindowID(s.window))
                s.minimized = true;
            if (event.type == SDL_EVENT_WINDOW_RESTORED && event.window.windowID == SDL_GetWindowID(s.window))
                s.minimized = false;
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(s.window)))
                s.running = false;
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.windowID == SDL_GetWindowID(s.window) &&
                !event.key.repeat && event.key.key == SDLK_O && (event.key.mod & SDL_KMOD_CTRL) && !s.files.isOpen() &&
                !s.aboutOpen)
                s.files.requestOpen();
            const bool keyEvent = event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP;
            if (s.inputCallbacks.event && (!keyEvent || event.key.windowID == SDL_GetWindowID(s.window)))
                s.inputCallbacks.event(event, acceptsInput());
        }
        return s.running;
    }

    bool Window::acceptsInput() const
    {
        const auto &s = *impl_;
        if (!s.contextReady || !s.running || !s.focused || s.minimized || s.files.isOpen() ||
            s.keyboardDialog.active() || s.aboutOpen)
            return false;
        const auto &io = ImGui::GetIO();
        return !io.WantCaptureKeyboard && !io.WantTextInput &&
               !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    }

    void Window::render()
    {
        auto &s = *impl_;
        if (!s.rendererReady)
            throw std::logic_error("Open the window before rendering.");
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        bool openAbout = false;
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Open...", "Ctrl+O"))
                    s.files.requestOpen();
                ImGui::Separator();
                if (ImGui::MenuItem("Exit"))
                    s.running = false;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Input"))
            {
                if (ImGui::BeginMenu("Player Controller", s.inputCallbacks.playerUsesJoystick && s.inputCallbacks.setPlayerJoystick))
                {
                    for (unsigned player = 0; player < 2; ++player)
                        if (ImGui::BeginMenu(player ? "Player 2" : "Player 1"))
                        {
                            const bool joystick = s.inputCallbacks.playerUsesJoystick(player);
                            if (ImGui::MenuItem("Keyboard", nullptr, !joystick))
                                s.inputCallbacks.setPlayerJoystick(player, false);
                            if (ImGui::MenuItem("Joystick", nullptr, joystick))
                                s.inputCallbacks.setPlayerJoystick(player, true);
                            ImGui::EndMenu();
                        }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Configure Keyboard..."))
                    s.keyboardDialog.open();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("About"))
            {
                openAbout = ImGui::MenuItem("About R2MEGA");
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        const auto *pixels = s.videoProvider ? s.videoProvider() : nullptr;
        if (s.videoTexture && pixels)
        {
            if (!SDL_UpdateTexture(s.videoTexture, nullptr, pixels, 320 * sizeof(std::uint32_t)))
                throw std::runtime_error(SDL_GetError());
        }
        const auto *viewport = ImGui::GetMainViewport();
        auto *background = ImGui::GetBackgroundDrawList();
        const auto status = pixels && s.videoStatusProvider ? s.videoStatusProvider() : VideoStatus{};
        const unsigned videoWidth = std::clamp(status.width, 1u, 320u);
        if (pixels && s.videoStatusProvider)
        {
            char counter[96];
            std::snprintf(counter, sizeof(counter), "Quadro: %llu  |  PC: $%06X",
                          static_cast<unsigned long long>(status.frame), status.pc);
            std::string message = counter;
            message += videoWidth == 320 ? "\nVDP parcial: planos A/B e sprites em H40."
                                         : "\nVDP parcial: planos A/B e sprites em H32.";
            if (!status.displayEnabled)
                message += "\nExibição da VDP ainda não ativada.";
            if (s.runningProvider && !s.runningProvider())
            {
                message += "\nEmulação pausada.";
                if (!status.error.empty())
                    message += " " + status.error;
            }
            const float wrapWidth = std::max(1.0f, viewport->WorkSize.x - 40.0f);
            const auto textSize = ImGui::CalcTextSize(message.c_str(), nullptr, false, wrapWidth);
            const ImVec2 position(viewport->WorkPos.x + 12.0f, viewport->WorkPos.y + 12.0f);
            background->AddRectFilled(position, ImVec2(position.x + std::min(wrapWidth, textSize.x) + 16.0f, position.y + textSize.y + 16.0f), IM_COL32(0, 0, 0, 210), 5.0f);
            background->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                                ImVec2(position.x + 8.0f, position.y + 8.0f), IM_COL32_WHITE,
                                message.c_str(), nullptr, wrapWidth);
        }
        else if (!pixels)
        {
            constexpr const char *prompt = "Use File > Open... (Ctrl+O) para carregar uma ROM.";
            const auto size = ImGui::CalcTextSize(prompt);
            background->AddText(ImVec2(viewport->WorkPos.x + (viewport->WorkSize.x - size.x) * 0.5f,
                                       viewport->WorkPos.y + (viewport->WorkSize.y - size.y) * 0.5f),
                                IM_COL32_WHITE, prompt);
        }
        if (openAbout)
        {
            s.aboutOpen = true;
            ImGui::OpenPopup("About R2MEGA");
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        bool aboutVisible = true;
        if (ImGui::BeginPopupModal("About R2MEGA", &aboutVisible, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::TextUnformatted("Software livre e aberto para a comunidade.");
            ImGui::TextUnformatted("Licen\xC3\xA7"
                                   "a: GNU GPL v3.0.");
            ImGui::Spacing();
            ImGui::TextUnformatted("Vers\xC3\xA3"
                                   "o: " R2_APP_VERSION);
            if (ImGui::Button("Close"))
            {
                s.aboutOpen = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (!aboutVisible)
            s.aboutOpen = false;
        s.files.draw();
        s.keyboardDialog.draw([&](const Systems::KeyboardMapping &mapping)
                              {
            if (!s.keyboardConfig.save(mapping)) return false;
            if (s.inputCallbacks.setKeyboardMapping) s.inputCallbacks.setKeyboardMapping(mapping);
            return true; });
        ImGui::Render();
        if (!SDL_SetRenderDrawColor(s.renderer, 0, 0, 0, 255) || !SDL_RenderClear(s.renderer))
            throw std::runtime_error(SDL_GetError());
        if (s.videoTexture && pixels)
        {
            int outputWidth = 0, outputHeight = 0;
            if (!SDL_GetRenderOutputSize(s.renderer, &outputWidth, &outputHeight))
                throw std::runtime_error(SDL_GetError());
            const float menuHeight = ImGui::GetFrameHeight() * ImGui::GetIO().DisplayFramebufferScale.y;
            const float availableHeight = std::max(0.0f, outputHeight - menuHeight);
            const float scale = std::min(outputWidth / float(videoWidth), availableHeight / 224.0f);
            if (scale > 0)
            {
                const SDL_FRect source{0, 0, float(videoWidth), 224};
                const SDL_FRect destination{(outputWidth - videoWidth * scale) * 0.5f,
                                            menuHeight + (availableHeight - 224.0f * scale) * 0.5f,
                                            videoWidth * scale, 224.0f * scale};
                if (!SDL_RenderTexture(s.renderer, s.videoTexture, &source, &destination))
                    throw std::runtime_error(SDL_GetError());
            }
        }
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), s.renderer);
        if (!SDL_RenderPresent(s.renderer))
            throw std::runtime_error(SDL_GetError());
    }
}