#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Interface temporária de inspeção da ROM, independente do futuro núcleo do 68000.
namespace R2MEGA::Window
{
    // Texto e quantidade de bytes consumidos pela prévia de uma instrução.
    struct PreviewInstruction
    {
        std::size_t length;
        std::string text;
    };
    // Decodifica um subconjunto de instruções; dados desconhecidos são exibidos como diretivas.
    PreviewInstruction decodePreview(const std::vector<std::uint8_t> &rom, std::size_t address);
    class RomPreview
    {
    public:
        // Usa o vetor de reset quando ele aponta para um endereço válido e alinhado na ROM.
        void reset(const std::vector<std::uint8_t> &rom);
        void draw(const std::vector<std::uint8_t> &rom);

    private:
        unsigned address_ = 0;
        bool validReset_ = false;
    };
}