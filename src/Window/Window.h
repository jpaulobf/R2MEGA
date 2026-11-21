#pragma once
#include <memory>
#include <filesystem>
#include <functional>
#include <string>
#include <cstdint>

namespace R2MEGA::Window
{
    // Gerencia a janela, seus eventos e a apresentação da interface.
    class Window
    {
    public:
        // Carrega o arquivo selecionado e retorna o nome a ser exibido.
        using OpenHandler = std::function<std::string(const std::filesystem::path &)>;

        // Quadro ARGB8888 de 256 x 224 pixels pertencente ao núcleo.
        using VideoProvider = std::function<const std::uint32_t *()>;
        using RunningProvider = std::function<bool()>;
        // Dados de execução mostrados junto ao quadro da VDP.
        struct VideoStatus
        {
            std::uint64_t frame = 0;
            std::uint32_t pc = 0;
            bool displayEnabled = false;
            std::string error;
        };
        using VideoStatusProvider = std::function<VideoStatus()>;

        explicit Window(OpenHandler openHandler = {}, VideoProvider videoProvider = {}, RunningProvider runningProvider = {},
                        VideoStatusProvider videoStatusProvider = {});

        ~Window();

        Window(const Window &) = delete;

        Window &operator=(const Window &) = delete;

        // Inicializa a janela antes do processamento de eventos e da renderização.
        void open();

        // Retorna falso quando a aplicação deve encerrar.
        bool processEvents();
        void render();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}