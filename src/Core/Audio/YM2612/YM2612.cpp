#include "Core/Audio/YM2612/YM2612.h"
#include <ymfm_opn.h>
#include <algorithm>
#include <limits>

namespace R2MEGA::Core
{
    struct YM2612::Chip : ymfm::ymfm_interface
    {
        static constexpr unsigned clocksPerSample = ymfm::ym2612::fm_engine::OPERATORS *
                                                    ymfm::opna_registers::DEFAULT_PRESCALE;
        static constexpr auto inactive = std::numeric_limits<std::uint64_t>::max();

        Chip() { synth.reset(); }

        void ymfm_set_timer(std::uint32_t timer, std::int32_t duration) override
        {
            timerClocks[timer] = duration < 0 ? inactive : now + static_cast<std::uint32_t>(duration);
            updateNextEvent();
        }

        void ymfm_set_busy_end(std::uint32_t clocks) override { busyEnd = now + clocks; }
        bool ymfm_is_busy() override { return now < busyEnd; }

        void updateNextEvent()
        {
            nextEvent = std::min({nextSample, timerClocks[0], timerClocks[1]});
        }

        void clock()
        {
            if (++now < nextEvent)
                return;
            for (unsigned timer = 0; timer < timerClocks.size(); ++timer)
            {
                if (timerClocks[timer] <= now)
                {
                    timerClocks[timer] = inactive;
                    m_engine->engine_timer_expired(timer);
                }
            }
            if (nextSample == now)
            {
                ymfm::ym2612::output_data sample;
                synth.generate(&sample);
                // Nível de repouso introduzido pela descontinuidade do DAC do YM2612.
                constexpr int silence = 4 * 128 * 64 / 65;
                output = {sample.data[0] - silence, sample.data[1] - silence};
                nextSample += clocksPerSample;
            }
            updateNextEvent();
        }

        std::uint64_t now = 0, busyEnd = 0, nextSample = clocksPerSample, nextEvent = clocksPerSample;
        std::array<std::uint64_t, 2> timerClocks{inactive, inactive};
        std::array<std::int32_t, 2> output{};
        ymfm::ym2612 synth{*this};
    };

    YM2612::YM2612() { reset(); }
    YM2612::~YM2612() = default;

    void YM2612::reset()
    {
        // Reinicia também o latch e o DAC, externos ao motor FM.
        chip_ = std::make_unique<Chip>();
        address_ = 0;
        registers_.fill(0);
    }

    void YM2612::clock() { chip_->clock(); }
    std::uint8_t YM2612::readPort(unsigned port) const { return chip_->synth.read(port); }
    const std::array<std::int32_t, 2> &YM2612::sample() const { return chip_->output; }

    void YM2612::writePort(unsigned port, std::uint8_t value)
    {
        const unsigned bank = (port >> 1) & 1;
        if (!(port & 1))
            address_ = bank * 256 + value;
        else if (address_ / 256 == bank)
            registers_[address_] = value;
        chip_->synth.write(port, value);
    }
}