#pragma once

#include <array>
#include <cstdint>

namespace R2MEGA::Core
{
    // Registradores da área A10000-A1001F vistos pelo 68000.
    class ControllerPorts
    {
    public:
        void reset();
        std::uint8_t readByte(unsigned offset) const;
        void writeByte(unsigned offset, std::uint8_t value);

    private:
        // Cada registrador ocupa uma palavra, mas usa apenas o byte baixo.
        std::array<std::uint8_t, 16> registers_{};
    };
}