#pragma once

namespace R2MEGA::Core
{
    class M68KBus;

    // Motorola 68000 implementation to be developed against its bus contract.
    class Moto68000
    {
    public:
        explicit Moto68000(M68KBus &bus);
        Moto68000(const Moto68000 &) = delete;
        Moto68000 &operator=(const Moto68000 &) = delete;

    private:
        M68KBus &bus_;
    };
}
