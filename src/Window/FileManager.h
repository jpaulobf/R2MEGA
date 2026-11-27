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
        // Abre o navegador ou solicita o carregamento de uma ROM recente.
        void requestOpen(std::filesystem::path file = {});
        // Desenha o navegador e processa a seleção e a navegação.
        void draw();
        bool isOpen() const { return requested_ || open_; }
        const std::vector<std::filesystem::path> &recentFiles() const { return recentFiles_; }

    private:
        struct Entry
        {
            std::filesystem::path path;
            std::string name;
            bool directory;
        };
        void navigate(const std::filesystem::path &path);
        void browseLastDirectory();
        void load(const std::filesystem::path &path);
        void readConfig();
        void saveConfig() const;

        OpenHandler openHandler_;
        std::filesystem::path directory_;
        std::vector<Entry> entries_;
        std::vector<std::filesystem::path> recentFiles_;
        std::filesystem::path lastRomDirectory_, pendingFile_;
        std::string error_;
        bool requested_ = false, open_ = false;
    };
}