#pragma once

#include <array>
#include <cstdint>

namespace R2MEGA::Core
{
    // PSG integrado ao VDP: três tons, um canal de ruído e quatro atenuadores.
    class SN76489
    {
    public:
        void reset();
        void write(std::uint8_t value);
        // Um tick de entrada a cada 15 ciclos mestres; o chip divide por 16.
        void clock();
        std::int16_t sample() const;
        std::uint16_t registerValue(unsigned index) const { return registers_.at(index); }
        std::uint16_t noiseShiftRegister() const { return noiseShift_; }

    private:
        unsigned period(unsigned channel) const;
        std::array<std::uint16_t, 8> registers_{};
        std::array<bool, 4> volumeWritten_{};
        std::array<unsigned, 4> counters_{};
        std::array<bool, 4> polarity_{};
        std::uint16_t noiseShift_ = 0x8000;
        unsigned divider_ = 0;
        unsigned latched_ = 3;
    };
}