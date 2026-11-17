#pragma once

#include <cstdint>

namespace R2MEGA::Core
{
    // Byte and word transfers are distinct: the 68000 has byte lane strobes.
    // Timing, alignment errors and arbitration will be defined with the CPU.
    class M68KBus
    {
    public:
        virtual ~M68KBus() = default;
        virtual std::uint8_t readByte(std::uint32_t address) = 0;
        virtual std::uint16_t readWord(std::uint32_t address) = 0;
        virtual void writeByte(std::uint32_t address, std::uint8_t value) = 0;
        virtual void writeWord(std::uint32_t address, std::uint16_t value) = 0;
    };
}
