#pragma once

namespace R2MEGA::Core
{
    class Z80Bus;

    // Z80 implementation to be developed against its own bus contract.
    class Z80
    {
    public:
        explicit Z80(Z80Bus &bus);
        Z80(const Z80 &) = delete;
        Z80 &operator=(const Z80 &) = delete;

    private:
        Z80Bus &bus_;
    };
}
