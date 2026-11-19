#include "Core/Scheduler/Scheduler.h"

namespace R2MEGA::Core
{
    void Scheduler::reset()
    {
        masterCycles_ = 0;
        m68kPhase_ = 0;
        z80Phase_ = 0;
    }

    void Scheduler::clock()
    {
        ++masterCycles_;
        const bool m68k = ++m68kPhase_ == M68KDivider;
        const bool z80 = ++z80Phase_ == Z80Divider;
        if (m68k)
            m68kPhase_ = 0;
        if (z80)
            z80Phase_ = 0;

        target_.clockVDP();
        if (m68k)
            target_.clockM68K();
        if (z80)
            target_.clockZ80();
    }

    void Scheduler::advance(std::uint64_t masterCycles)
    {
        while (masterCycles-- != 0)
            clock();
    }
}