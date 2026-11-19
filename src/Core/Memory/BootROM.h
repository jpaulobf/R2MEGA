#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace R2MEGA::Core
{
    // Imagem opcional fornecida externamente; não define mapeamento nem ativação.
    class BootROM
    {
    public:
        explicit BootROM(std::vector<std::uint8_t> bytes = {}) : bytes_(std::move(bytes)) {}
        std::size_t size() const { return bytes_.size(); }
        std::uint8_t read(std::size_t offset) const { return bytes_.at(offset); }

    private:
        std::vector<std::uint8_t> bytes_;
    };
}