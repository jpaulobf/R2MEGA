#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace R2MEGA::Core
{
    // Armazena a imagem normalizada da ROM e sua origem, independentemente do barramento.
    class Cartridge
    {
    public:
        struct Header
        {
            static constexpr std::size_t offset = 0x100;
            std::string console;
            std::string domesticTitle;
            std::string overseasTitle;
            std::string productCode;
            std::string region;
            std::uint16_t checksum = 0;
        };

        Cartridge() = default;
        // Carrega a ROM e converte imagens SMD para a ordem normal de bytes; lança em caso de falha.
        static Cartridge load(const std::filesystem::path &path);
        // Caminho do arquivo de origem e nome da ROM, inclusive quando está em um ZIP.
        const std::filesystem::path &path() const { return path_; }
        const std::string &romName() const { return romName_; }
        // Lê o cabeçalho opcional da imagem já normalizada.
        std::optional<Header> header() const;
        // A referência permanece válida enquanto o cartucho e sua imagem existirem.
        const std::vector<std::uint8_t> &rom() const { return rom_; }

    private:
        std::filesystem::path path_;
        std::string romName_;
        std::vector<std::uint8_t> rom_;
    };
}