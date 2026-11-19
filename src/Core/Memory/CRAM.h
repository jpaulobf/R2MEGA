#pragma once

#include "Core/Memory/Memory.h"

namespace R2MEGA::Core
{
    // Memória das entradas de cor do VDP.
    class CRAM final : public WordMemory<64>
    {
    };
}