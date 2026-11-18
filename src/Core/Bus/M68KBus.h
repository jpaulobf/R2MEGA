#pragma once

#include <cstdint>

namespace R2MEGA::Core
{
    // As transferências de byte e palavra são distintas: o 68000 possui sinais de seleção de bytes.
    // Temporização, erros de alinhamento e arbitragem serão definidos com a CPU.
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
