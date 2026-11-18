#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace R2MEGA::Core
{
    // Armazena a imagem normalizada da ROM e sua origem, independentemente do barramento.
    class Cartridge
    {
    public:
        Cartridge() = default;
        // Carrega a ROM e converte imagens SMD para a ordem normal de bytes; lança em caso de falha.
        static Cartridge load(const std::filesystem::path &path);
        // Caminho do arquivo de origem e nome da ROM, inclusive quando está em um ZIP.
        const std::filesystem::path &path() const { return path_; }
        const std::string &romName() const { return romName_; }
        // A referência permanece válida enquanto o cartucho e sua imagem existirem.
        const std::vector<std::uint8_t> &rom() const { return rom_; }

    private:
        std::filesystem::path path_;
        std::string romName_;
        std::vector<std::uint8_t> rom_;
    };
}