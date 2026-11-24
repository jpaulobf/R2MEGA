#pragma once

#include <cstdint>
#include <stdexcept>

namespace R2MEGA::Systems
{
    // Agenda quadros em nanossegundos, preservando a fração entre prazos.
    class FramePacer
    {
    public:
        static constexpr unsigned maxFramesPerPresent = 4;

        FramePacer(unsigned clockFrequency, unsigned cyclesPerFrame)
            : clockFrequency_(clockFrequency), frameNumerator_(cyclesPerFrame * 1000000000ull)
        {
            if (!clockFrequency || !cyclesPerFrame)
                throw std::invalid_argument("Invalid frame timing.");
        }

        void reset(std::uint64_t now)
        {
            deadline_ = now;
            remainder_ = 0;
        }

        bool takeFrame(std::uint64_t now)
        {
            if (now < deadline_)
                return false;
            // Recupera atrasos curtos e reinicia a agenda após uma espera longa.
            if (now - deadline_ > maxFramesPerPresent * (frameNumerator_ / clockFrequency_))
                reset(now);
            remainder_ += frameNumerator_;
            deadline_ += remainder_ / clockFrequency_;
            remainder_ %= clockFrequency_;
            return true;
        }

        std::uint64_t deadline() const { return deadline_; }

    private:
        unsigned clockFrequency_;
        std::uint64_t frameNumerator_;
        std::uint64_t deadline_ = 0, remainder_ = 0;
    };
}