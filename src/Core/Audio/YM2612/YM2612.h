#pragma once

#include <array>
#include <cstdint>

namespace R2MEGA::Core
{
    // Estado mínimo das portas do sintetizador FM YM2612.
    class YM2612
    {
    public:
        void reset()
        {
            status_ = 0;
            address_.fill(0);
            registers_.fill(0);
        }
        // Os temporizadores e o sinal de ocupado serão ligados ao núcleo de síntese.
        unsigned readStatus() const { return status_; }
        void writePort(unsigned port, std::uint8_t value)
        {
            const unsigned bank = (port >> 1) & 1;
            if (port & 1)
                registers_[bank * 256 + address_[bank]] = value;
            else
                address_[bank] = value;
        }
        std::uint8_t registerValue(unsigned bank, unsigned index) const { return registers_.at(bank * 256 + index); }

    private:
        unsigned status_ = 0;
        std::array<std::uint8_t, 2> address_{};
        std::array<std::uint8_t, 512> registers_{};
    };
}