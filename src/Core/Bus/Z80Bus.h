#pragma once

#include <cstdint>

namespace R2MEGA::Core
{
    // O Z80 possui espaço de endereçamento próprio e compartilha alguns dispositivos com o 68000.
    class Z80Bus
    {
    public:
        virtual ~Z80Bus() = default;
        virtual std::uint8_t readMemory(std::uint16_t address) = 0;
        virtual void writeMemory(std::uint16_t address, std::uint8_t value) = 0;
        
        // Acesso ao espaço de portas de entrada e saída do Z80.
        virtual std::uint8_t input(std::uint16_t port) = 0;
        virtual void output(std::uint16_t port, std::uint8_t value) = 0;
    };
}
