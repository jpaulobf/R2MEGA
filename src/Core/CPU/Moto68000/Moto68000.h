#pragma once

namespace R2MEGA::Core
{
    class M68KBus;

    // Implementação do Motorola 68000 a ser desenvolvida com sua interface de barramento.
    class Moto68000
    {
    public:
        explicit Moto68000(M68KBus &bus);
        Moto68000(const Moto68000 &) = delete;
        Moto68000 &operator=(const Moto68000 &) = delete;

    private:
        // Referência não proprietária; o barramento deve sobreviver à CPU.
        M68KBus &bus_;
    };
}
