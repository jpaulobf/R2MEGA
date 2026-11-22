#pragma once
#include "Core/IO/ControllerInput.h"
#include <cstdint>

namespace R2MEGA::Core
{
    // Controle de três botões: TH seleciona as duas metades das linhas ativas em zero.
    class Joypad
    {
    public:
        static std::uint8_t read(std::uint16_t buttons, bool thHigh)
        {
            std::uint8_t lines = thHigh ? 0x7f : 0x33;
            if (buttons & ControllerInput::Up)
                lines &= ~std::uint8_t(0x01);
            if (buttons & ControllerInput::Down)
                lines &= ~std::uint8_t(0x02);
            if (thHigh)
            {
                if (buttons & ControllerInput::Left)
                    lines &= ~std::uint8_t(0x04);
                if (buttons & ControllerInput::Right)
                    lines &= ~std::uint8_t(0x08);
                if (buttons & ControllerInput::B)
                    lines &= ~std::uint8_t(0x10);
                if (buttons & ControllerInput::C)
                    lines &= ~std::uint8_t(0x20);
            }
            else
            {
                if (buttons & ControllerInput::A)
                    lines &= ~std::uint8_t(0x10);
                if (buttons & ControllerInput::Start)
                    lines &= ~std::uint8_t(0x20);
            }
            return lines;
        }
    };
}