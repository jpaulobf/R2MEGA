#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace R2MEGA::Core
{
    // Armazenamento físico em bytes; endereçamento e espelhamento pertencem ao dispositivo.
    template <std::size_t Size>
    class FixedMemory
    {
    public:
        static constexpr std::size_t size() { return Size; }
        std::uint8_t read(std::size_t offset) const { return bytes_.at(offset); }
        void write(std::size_t offset, std::uint8_t value) { bytes_.at(offset) = value; }
        void clear() { bytes_.fill(0); }

    private:
        std::array<std::uint8_t, Size> bytes_{};
    };

    // Entradas do VDP armazenadas como palavras; máscaras e acesso por portas serão do VDP.
    template <std::size_t Count>
    class WordMemory
    {
    public:
        static constexpr std::size_t size() { return Count; }
        std::uint16_t read(std::size_t index) const { return words_.at(index); }
        void write(std::size_t index, std::uint16_t value) { words_.at(index) = value; }
        void clear() { words_.fill(0); }

    private:
        std::array<std::uint16_t, Count> words_{};
    };
}