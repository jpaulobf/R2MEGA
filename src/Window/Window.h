#pragma once
#include <memory>
#include <filesystem>
#include <functional>
#include <string>
#include <cstdint>
#include <vector>

namespace R2MEGA::Window
{
    // Gerencia a janela, seus eventos e a apresentação da interface.
    class Window
    {
    public:
        // Carrega o arquivo selecionado e retorna o nome a ser exibido.
        using OpenHandler = std::function<std::string(const std::filesystem::path &)>;
        
        // Acesso temporário para inspeção; a aplicação continua proprietária da ROM.
        using RomProvider = std::function<const std::vector<std::uint8_t> *()>;
        
        explicit Window(OpenHandler openHandler = {}, RomProvider romProvider = {});
        
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