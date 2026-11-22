#pragma once
#include <memory>
#include <filesystem>
#include <functional>
#include <string>
#include <cstdint>
#include <SDL3/SDL.h>
#include "Systems/KeyboardMapping.h"

namespace R2MEGA::Window
{
    // Gerencia a janela, seus eventos e a apresentação da interface.
    class Window
    {
    public:
        // Carrega o arquivo selecionado e retorna o nome a ser exibido.
        using OpenHandler = std::function<std::string(const std::filesystem::path &)>;

        // Quadro ARGB8888 com stride de 320 pixels e altura de 224 linhas.
        using VideoProvider = std::function<const std::uint32_t *()>;
        using RunningProvider = std::function<bool()>;
        // Dados de execução mostrados junto ao quadro da VDP.
        struct VideoStatus
        {
            std::uint64_t frame = 0;
            std::uint32_t pc = 0;
            unsigned width = 256;
            bool displayEnabled = false;
            std::string error;
        };
        using VideoStatusProvider = std::function<VideoStatus()>;
        struct InputCallbacks
        {
            std::function<void(const SDL_Event &, bool)> event;
            std::function<void(const Systems::KeyboardMapping &)> setKeyboardMapping;
            std::function<bool(unsigned)> playerUsesJoystick;
            std::function<void(unsigned, bool)> setPlayerJoystick;
        };

        explicit Window(OpenHandler openHandler = {}, VideoProvider videoProvider = {}, RunningProvider runningProvider = {},
                        VideoStatusProvider videoStatusProvider = {}, InputCallbacks inputCallbacks = {});

        ~Window();

        Window(const Window &) = delete;

        Window &operator=(const Window &) = delete;

        // Inicializa a janela antes do processamento de eventos e da renderização.
        void open();

        // Retorna falso quando a aplicação deve encerrar.
        bool processEvents();
        // Bloqueia a entrada de jogo enquanto a janela ou um diálogo captura o teclado.
        bool acceptsInput() const;
        void render();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}