#pragma once

#include "Core/Memory/Memory.h"

namespace R2MEGA::Core
{
    // Memória de vídeo acessada pelo VDP.
    class VRAM final : public FixedMemory<64 * 1024>
    {
    };
}