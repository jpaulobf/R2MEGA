#pragma once

#include <array>
#include <cstdint>
#include "Core/IO/ControllerInput.h"

namespace R2MEGA::Core
{
    // Registradores da área A10000-A1001F vistos pelo 68000.
    class ControllerPorts
    {
    public:
        void reset();
        std::uint8_t readByte(unsigned offset) const;
        void writeByte(unsigned offset, std::uint8_t value);
        
        // Atualiza as linhas de entrada sem alterar os registradores programados pela ROM.
        void setButtons(unsigned player, std::uint16_t buttons) { input_.setButtons(player, buttons); }
        std::uint16_t buttons(unsigned player) const { return input_.buttons(player); }

    private:
        // Cada registrador ocupa uma palavra, mas usa apenas o byte baixo.
        std::array<std::uint8_t, 16> registers_{};
        ControllerInput input_;
    };
}