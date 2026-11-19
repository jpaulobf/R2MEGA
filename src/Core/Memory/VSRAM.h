#pragma once

#include "Core/Memory/Memory.h"

namespace R2MEGA::Core
{
    // Memória dos valores de rolagem vertical do VDP.
    class VSRAM final : public WordMemory<40>
    {
    };
}