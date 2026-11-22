#include "Core/IO/ControllerPorts.h"
#include "Core/IO/Joypad.h"
#include <stdexcept>

namespace R2MEGA::Core
{
    void ControllerPorts::reset()
    {
        registers_.fill(0);
        input_.reset();
    }

    std::uint8_t ControllerPorts::readByte(unsigned offset) const
    {
        if (offset >= 0x20)
            throw std::out_of_range("Endereço fora da área de I/O");
        if ((offset & 1) == 0)
            return 0;

        const unsigned index = offset >> 1;
        if (index == 0)
            return 0xa0; // Modelo internacional NTSC, versão 0, sem unidade de disco.
        if (index >= 1 && index <= 3)
        {
            const std::uint8_t direction = registers_[index + 3];
            // TH em entrada permanece alto; em saída, usa o valor escrito pela ROM.
            const bool thHigh = !(direction & 0x40) || (registers_[index] & 0x40);
            const std::uint8_t lines = index <= 2
                                           ? Joypad::read(input_.buttons(index - 1), thHigh)
                                           : std::uint8_t(0x7f);
            return (registers_[index] & direction) | (lines & ~direction & 0x7f);
        }
        return registers_[index];
    }

    void ControllerPorts::writeByte(unsigned offset, std::uint8_t value)
    {
        if (offset >= 0x20)
            throw std::out_of_range("Endereço fora da área de I/O");
        if ((offset & 1) == 0)
            return;

        const unsigned index = offset >> 1;
        // Versão e dados recebidos pela interface serial são somente leitura.
        if (index == 0 || index == 8 || index == 11 || index == 14)
            return;
        registers_[index] = value;
    }
}