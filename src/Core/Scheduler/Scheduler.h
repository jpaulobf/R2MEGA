#pragma once

#include <cstdint>

namespace R2MEGA::Core
{
    // Receptor dos ciclos derivados. Cada chamada representa um ciclo, não uma instrução.
    class SchedulerTarget
    {
    public:
        virtual ~SchedulerTarget() = default;
        virtual void clockVDP() = 0;
        virtual void clockM68K() = 0;
        virtual void clockZ80() = 0;
    };

    // Distribui ciclos inteiros a partir do clock mestre, sem usar tempo de parede.
    class Scheduler
    {
    public:
        static constexpr unsigned MasterClock = 53693175;
        static constexpr unsigned M68KDivider = 7;
        static constexpr unsigned Z80Divider = 15;

        // O receptor deve permanecer válido durante toda a vida do scheduler.
        explicit Scheduler(SchedulerTarget &target) : target_(target) {}
        Scheduler(const Scheduler &) = delete;
        Scheduler &operator=(const Scheduler &) = delete;

        // Reinicia apenas a fase e a contagem do scheduler, sem reiniciar os componentes.
        void reset();
        // Avança um ciclo mestre. Na coincidência, a ordem é VDP, 68000 e Z80.
        void clock();
        void advance(std::uint64_t masterCycles);
        std::uint64_t masterCycles() const { return masterCycles_; }

    private:
        SchedulerTarget &target_;
        std::uint64_t masterCycles_ = 0;
        unsigned m68kPhase_ = 0;
        unsigned z80Phase_ = 0;
    };
}