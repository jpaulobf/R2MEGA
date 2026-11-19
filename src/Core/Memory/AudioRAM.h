#pragma once

#include "Core/Memory/Memory.h"

namespace R2MEGA::Core
{
    // Memória de trabalho do Z80.
    class AudioRAM final : public FixedMemory<8 * 1024>
    {
    };
}