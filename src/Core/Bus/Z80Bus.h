#pragma once

#include <cstdint>

namespace R2MEGA::Core
{
    // The Z80 has its own address space and shares selected devices with the 68000.
    class Z80Bus
    {
    public:
        virtual ~Z80Bus() = default;
        virtual std::uint8_t readMemory(std::uint16_t address) = 0;
        virtual void writeMemory(std::uint16_t address, std::uint8_t value) = 0;
        virtual std::uint8_t input(std::uint16_t port) = 0;
        virtual void output(std::uint16_t port, std::uint8_t value) = 0;
    };
}
