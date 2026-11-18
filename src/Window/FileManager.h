#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace R2MEGA::Window
{
    // Navegador de arquivos usado pela interface para selecionar uma ROM.
    class FileManager
    {
    public:
        // Chamado com o caminho selecionado; exceções são exibidas na interface.
        using OpenHandler = std::function<void(const std::filesystem::path &)>;
        explicit FileManager(OpenHandler handler);
        // Solicita a abertura do navegador no próximo desenho da interface.
        void requestOpen();
        // Desenha o navegador e processa a seleção e a navegação.
        void draw();
        bool isOpen() const { return requested_ || open_; }

    private:
        struct Entry
        {
            std::filesystem::path path;
            std::string name;
            bool directory;
        };
        void navigate(const std::filesystem::path &path);
        OpenHandler openHandler_;
        std::filesystem::path directory_;
        std::vector<Entry> entries_;
        std::string error_;
        bool requested_ = false, open_ = false;
    };
}