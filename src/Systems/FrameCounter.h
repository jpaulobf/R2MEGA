#pragma once

#include <chrono>
#include <cstdint>

namespace R2MEGA::Systems
{
    // Mede quadros emulados por segundo real, mesmo quando a apresentação pula quadros.
    class FrameCounter
    {
    public:
        using Clock = std::chrono::steady_clock;

        explicit FrameCounter(Clock::time_point now = Clock::now()) { reset(now); }

        void reset(Clock::time_point now)
        {
            start_ = now;
            frames_ = 0;
            fps_ = 0;
        }

        void excludePause(Clock::duration duration)
        {
            if (duration > Clock::duration::zero())
                start_ += duration;
        }

        // Atualiza a média a cada quadro e renova a amostra após meio segundo.
        void record(Clock::time_point now)
        {
            ++frames_;
            const auto elapsed = now - start_;
            if (elapsed <= Clock::duration::zero())
                return;
            fps_ = frames_ / std::chrono::duration<double>(elapsed).count();
            if (elapsed >= std::chrono::milliseconds(500))
            {
                frames_ = 0;
                start_ = now;
            }
        }

        double fps() const { return fps_; }

    private:
        Clock::time_point start_;
        std::uint64_t frames_ = 0;
        double fps_ = 0;
    };
}