#pragma once

#include <array>
#include <cstdint>
#include <memory>

namespace R2MEGA::Core
{
    // Portas, temporização e saída estéreo do sintetizador FM e DAC.
    class YM2612
    {
    public:
        YM2612();
        ~YM2612();
        void reset();
        // Um clock de entrada a cada sete clocks mestres do Mega Drive.
        void clock();
        std::uint8_t readPort(unsigned port) const;
        void writePort(unsigned port, std::uint8_t value);
        const std::array<std::int32_t, 2> &sample() const;
        std::uint8_t registerValue(unsigned bank, unsigned index) const { return registers_.at(bank * 256 + index); }

    private:
        struct Chip;
        std::unique_ptr<Chip> chip_;
        unsigned address_ = 0;
        std::array<std::uint8_t, 512> registers_{};
    };
}