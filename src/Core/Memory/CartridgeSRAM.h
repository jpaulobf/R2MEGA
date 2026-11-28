#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace R2MEGA::Core
{
    // Capacidade definida pelo cartucho; persistência e mapeamento não são tratados aqui.
    class CartridgeSRAM
    {
    public:
        explicit CartridgeSRAM(std::size_t size = 0, std::uint8_t initialValue = 0)
            : bytes_(size, initialValue) {}
        std::size_t size() const { return bytes_.size(); }
        std::uint8_t read(std::size_t offset) const { return bytes_.at(offset); }
        void write(std::size_t offset, std::uint8_t value) { bytes_.at(offset) = value; }
        void clear() { std::fill(bytes_.begin(), bytes_.end(), 0); }

    private:
        std::vector<std::uint8_t> bytes_;
    };
}