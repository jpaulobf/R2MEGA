#include "Core/VDP/VDP.h"
#include "Systems/FramePacer.h"
#include <cstdint>
#include <initializer_list>
#include <stdexcept>

namespace
{
    using R2MEGA::Systems::FramePacer;
    constexpr unsigned masterClock = 53693175;
    constexpr unsigned cyclesPerFrame = R2MEGA::Core::VDP::masterCyclesPerLine * R2MEGA::Core::VDP::linesPerFrame;
    constexpr std::uint64_t nanosecondsPerSecond = 1000000000;

    void require(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void checkPresentation(unsigned refreshRate)
    {
        FramePacer pacer(masterClock, cyclesPerFrame);
        pacer.reset(0);
        unsigned totalFrames = 0;
        bool repeated = false, batched = false;
        for (unsigned refresh = 0; refresh <= 60 * refreshRate; ++refresh)
        {
            const auto now = refresh * nanosecondsPerSecond / refreshRate;
            unsigned frames = 0;
            while (frames < FramePacer::maxFramesPerPresent && pacer.takeFrame(now))
                ++frames;
            totalFrames += frames;
            repeated |= frames == 0;
            batched |= frames > 1;
        }
        require(totalFrames == 1 + 60ull * masterClock / cyclesPerFrame,
                "The monitor refresh rate must not change how many frames are emulated");
        if (refreshRate * cyclesPerFrame > masterClock)
            require(repeated, "A faster display must sometimes reuse the previous frame");
        else
            require(batched, "A slower display must sometimes run multiple frames before presentation");
    }

    void checkVSyncJitter()
    {
        FramePacer pacer(masterClock, cyclesPerFrame);
        constexpr unsigned refreshRate = 60;
        constexpr auto displayLead = nanosecondsPerSecond / refreshRate / 2;
        constexpr std::uint64_t jitter = 1000000;
        pacer.reset(jitter);
        unsigned totalFrames = 0;
        for (unsigned refresh = 0; refresh <= 60 * refreshRate; ++refresh)
        {
            const auto now = refresh * nanosecondsPerSecond / refreshRate +
                             (refresh % 2 ? 0 : 2 * jitter);
            if (pacer.takeFrame(now, displayLead))
            {
                ++totalFrames;
                require(!pacer.takeFrame(now), "VSync jitter must not cause a catch-up frame at 60 Hz");
            }
        }
        require(totalFrames == 1 + 60ull * masterClock / cyclesPerFrame,
                "VSync lead must not accumulate or change the native rate");

        pacer.reset(0);
        require(pacer.takeFrame(0, displayLead), "The first frame must be ready immediately");
        const auto delayed = 3 * nanosecondsPerSecond / refreshRate;
        require(pacer.takeFrame(delayed, displayLead) && pacer.takeFrame(delayed),
                "A missed presentation must still allow overdue frames to catch up");
    }
}

int main()
{
    for (const unsigned refreshRate : {30, 60, 120, 144})
        checkPresentation(refreshRate);
    checkVSyncJitter();

    FramePacer pacer(masterClock, cyclesPerFrame);
    constexpr std::uint64_t start = 100 * nanosecondsPerSecond;
    pacer.reset(start);
    for (std::uint64_t frame = 1; frame <= 10000; ++frame)
    {
        require(pacer.takeFrame(pacer.deadline()), "A frame must run at its deadline");
        const auto expected = start + frame * cyclesPerFrame * nanosecondsPerSecond / masterClock;
        require(pacer.deadline() == expected, "Frame deadlines must retain fractional nanoseconds");
        require(!pacer.takeFrame(expected - 1), "A frame must not run before its deadline");
    }

    const auto afterStall = pacer.deadline() + nanosecondsPerSecond;
    require(pacer.takeFrame(afterStall), "The emulation must resume after a stall");
    require(!pacer.takeFrame(afterStall), "A long stall must discard expired deadlines");
    require(pacer.deadline() == afterStall + cyclesPerFrame * nanosecondsPerSecond / masterClock,
            "The schedule must resume at the native rate after a stall");
}