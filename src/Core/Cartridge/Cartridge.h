#pragma once
#include "Core/Memory/CartridgeSRAM.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace R2MEGA::Core
{
    // Armazena a imagem normalizada da ROM, sua origem e o mapa do cartucho.
    class Cartridge
    {
    public:
        static constexpr std::uint32_t windowEnd = 0x3fffff;
        static constexpr std::uint32_t saveControlAddress = 0xa130f1;

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

        std::uint8_t readByte(std::uint32_t address) const;
        void writeByte(std::uint32_t address, std::uint8_t value);
        void writeSaveControl(std::uint8_t value);
        void resetMapper();

    private:
        bool selectsSaveRam(std::uint32_t address) const;
        std::filesystem::path path_;
        std::string romName_;
        std::vector<std::uint8_t> rom_;
        std::uint32_t romMask_ = 0;
        CartridgeSRAM saveRam_;
        bool bankedSaveRam_ = false;
        bool saveRamEnabled_ = true;
    };
}