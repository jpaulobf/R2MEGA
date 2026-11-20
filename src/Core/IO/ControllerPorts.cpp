#include "Core/IO/ControllerPorts.h"
#include <stdexcept>

namespace R2MEGA::Core
{
    void ControllerPorts::reset()
    {
        registers_.fill(0);
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
            // Entradas sem botões pressionados ficam em nível alto.
            return (registers_[index] & direction) | (0x7f & ~direction);
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