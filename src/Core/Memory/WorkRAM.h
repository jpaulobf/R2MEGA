#pragma once

#include "Core/Memory/Memory.h"

namespace R2MEGA::Core
{
    // Memória principal de trabalho.
    class WorkRAM final : public FixedMemory<64 * 1024>
    {
    };
}