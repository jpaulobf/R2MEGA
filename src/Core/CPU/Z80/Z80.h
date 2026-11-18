#pragma once

namespace R2MEGA::Core
{
    class Z80Bus;

    // Implementação do Z80 a ser desenvolvida com sua interface de barramento.
    class Z80
    {
    public:
        explicit Z80(Z80Bus &bus);
        Z80(const Z80 &) = delete;
        Z80 &operator=(const Z80 &) = delete;

    private:
        // Referência não proprietária; o barramento deve sobreviver à CPU.
        Z80Bus &bus_;
    };
}
