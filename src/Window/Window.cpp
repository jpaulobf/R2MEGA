#include "Window/Window.h"
#include "Window/FileManager.h"
#include "Window/RomPreview.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace R2MEGA::Window
{
    struct Window::Impl
    {
        SDL_Window *window = nullptr;
        SDL_Renderer *renderer = nullptr;
        bool sdlReady = false, contextReady = false, platformReady = false, rendererReady = false;
        bool running = true;
        bool aboutOpen = false;
        OpenHandler openHandler;
        RomProvider romProvider;
        RomPreview preview;
        FileManager files;
        explicit Impl(OpenHandler handler, RomProvider provider)
            : openHandler(std::move(handler)), romProvider(std::move(provider)), files([this](const std::filesystem::path &path)
                                                                                       {
                if (!openHandler) throw std::runtime_error("ROM loading is unavailable.");
                const auto name = openHandler(path);
                if (romProvider)
                    if (const auto *rom = romProvider()) preview.reset(*rom);
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
            if (renderer)
                SDL_DestroyRenderer(renderer);
            if (window)
                SDL_DestroyWindow(window);
            if (sdlReady)
                SDL_Quit();
        }
    };

    Window::Window(OpenHandler openHandler, RomProvider romProvider)
        : impl_(std::make_unique<Impl>(std::move(openHandler), std::move(romProvider))) {}
    Window::~Window() = default;

    void Window::open()
    {
        auto &s = *impl_;
        if (s.sdlReady)
            throw std::logic_error("Window session has already been initialized.");
        if (!SDL_Init(SDL_INIT_VIDEO))
            throw std::runtime_error(SDL_GetError());
        s.sdlReady = true;
        s.window = SDL_CreateWindow("R2MEGA v." R2_APP_VERSION, 960, 672,
                                    SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!s.window)
            throw std::runtime_error(SDL_GetError());
        s.renderer = SDL_CreateRenderer(s.window, nullptr);
        if (!s.renderer)
            throw std::runtime_error(SDL_GetError());
        if (!SDL_SetRenderVSync(s.renderer, 1))
            throw std::runtime_error(SDL_GetError());
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        s.contextReady = true;
        auto &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
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
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(s.window)))
                s.running = false;
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.windowID == SDL_GetWindowID(s.window) &&
                !event.key.repeat && event.key.key == SDLK_O && (event.key.mod & SDL_KMOD_CTRL) && !s.files.isOpen() &&
                !s.aboutOpen)
                s.files.requestOpen();
        }
        return s.running;
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
            if (ImGui::BeginMenu("About"))
            {
                openAbout = ImGui::MenuItem("About R2MEGA");
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        if (s.romProvider)
            if (const auto *rom = s.romProvider())
                s.preview.draw(*rom);
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
        ImGui::Render();
        if (!SDL_SetRenderDrawColor(s.renderer, 0, 0, 0, 255) || !SDL_RenderClear(s.renderer))
            throw std::runtime_error(SDL_GetError());
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), s.renderer);
        if (!SDL_RenderPresent(s.renderer))
            throw std::runtime_error(SDL_GetError());
    }
}