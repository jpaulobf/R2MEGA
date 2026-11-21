#include "Core/Audio/SN76489/SN76489.h"

namespace R2MEGA::Core
{
    void SN76489::reset()
    {
        registers_.fill(0);
        volumeWritten_.fill(false); // A saída começa muda até receber os atenuadores.
        counters_.fill(1);
        polarity_.fill(false);
        noiseShift_ = 0x8000;
        divider_ = 0;
        latched_ = 3;
    }

    void SN76489::write(std::uint8_t value)
    {
        if (value & 0x80)
            latched_ = (value >> 4) & 7;
        auto &reg = registers_[latched_];
        if ((latched_ & 1) != 0)
        {
            reg = value & 15;
            volumeWritten_[latched_ >> 1] = true;
        }
        else if (latched_ == 6)
        {
            reg = value & 7;
            noiseShift_ = 0x8000;
            polarity_[3] = false;
        }
        else if (value & 0x80)
            reg = (reg & 0x3f0) | (value & 15);
        else
            reg = (reg & 15) | ((value & 0x3f) << 4);
    }

    unsigned SN76489::period(unsigned channel) const
    {
        if (channel < 3)
            return registers_[channel * 2] ? registers_[channel * 2] : 1;
        const unsigned rate = registers_[6] & 3;
        return rate == 3 ? period(2) : 16u << rate;
    }

    void SN76489::clock()
    {
        if (++divider_ != 16)
            return;
        divider_ = 0;
        for (unsigned channel = 0; channel < 4; ++channel)
        {
            if (--counters_[channel] != 0)
                continue;
            counters_[channel] = period(channel);
            polarity_[channel] = !polarity_[channel];
            if (channel == 3 && polarity_[3])
            {
                const unsigned outgoing = noiseShift_ & 1;
                const unsigned feedback = (registers_[6] & 4)
                                              ? outgoing ^ ((noiseShift_ >> 3) & 1)
                                              : outgoing;
                noiseShift_ = std::uint16_t((noiseShift_ >> 1) | (feedback << 15));
            }
        }
    }

    std::int16_t SN76489::sample() const
    {
        // Degraus de aproximadamente 2 dB; saída bruta para o futuro mixer.
        static constexpr std::array<int, 16> levels{
            2048, 1627, 1292, 1026, 815, 647, 514, 408,
            324, 257, 204, 162, 129, 102, 81, 0};
        int mixed = 0;
        for (unsigned channel = 0; channel < 3; ++channel)
        {
            const int level = volumeWritten_[channel] ? levels[registers_[channel * 2 + 1]] : 0;
            mixed += polarity_[channel] ? level : -level;
        }
        const int noiseLevel = volumeWritten_[3] ? levels[registers_[7]] : 0;
        mixed += (noiseShift_ & 1) ? noiseLevel : -noiseLevel;
        return static_cast<std::int16_t>(mixed);
    }
}