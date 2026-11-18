#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>
#include <string>

namespace R2MEGA::Common
{
    // Limite de alocação para imagens brutas; não representa um limite do hardware.
    inline constexpr std::size_t MaxRomFileSize = 16 * 1024 * 1024;

    // Limite para arquivos ZIP lidos em memória.
    inline constexpr std::size_t MaxZipFileSize = 128u * 1024 * 1024;
    // Lê a ROM ou a primeira entrada suportada de um ZIP; lança em caso de falha.
    std::vector<std::uint8_t> readRomFile(const std::filesystem::path &path, std::string *romName = nullptr);
    // Extrai a primeira ROM suportada; em caso de falha, preserva as saídas e retorna falso.
    bool extractRomFromZip(const std::vector<std::uint8_t> &zip, std::vector<std::uint8_t> &rom, std::string &romName);
}