#include "Core/Memory/WorkRAM.h"
#include "Core/Memory/AudioRAM.h"
#include "Core/Memory/VRAM.h"
#include "Core/Memory/CRAM.h"
#include "Core/Memory/VSRAM.h"
#include "Core/Memory/BootROM.h"
#include "Core/Memory/CartridgeSRAM.h"
#include <type_traits>
#include <stdexcept>
#include <iostream>

using namespace R2MEGA::Core;

static_assert(!std::is_same<WorkRAM, VRAM>::value, "As memórias devem ter tipos distintos");

static void require(bool condition)
{
    if (!condition)
        throw std::runtime_error("Falha no teste de memoria");
}

template <typename Memory>
static void testStorage(Memory &memory)
{
    require(memory.read(0) == 0);
    memory.write(0, 0x12);
    memory.write(memory.size() - 1, 0x34);
    require(memory.read(0) == 0x12 && memory.read(memory.size() - 1) == 0x34);
    bool rejected = false;
    try { memory.write(memory.size(), 1); }
    catch (const std::out_of_range &) { rejected = true; }
    require(rejected);
    memory.clear();
    require(memory.read(0) == 0 && memory.read(memory.size() - 1) == 0);
}

int main()
{
    try
    {
        WorkRAM work;
        AudioRAM audio;
        VRAM video;
        CRAM colors;
        VSRAM scroll;
        CartridgeSRAM save(13);
        require(work.size() == 65536 && audio.size() == 8192 && video.size() == 65536);
        require(colors.size() == 64 && scroll.size() == 40);
        testStorage(work); testStorage(audio); testStorage(video);
        testStorage(colors); testStorage(scroll); testStorage(save);
        colors.write(0, 0x1234);
        require(colors.read(0) == 0x1234);
        BootROM boot({0x12, 0x34});
        require(boot.size() == 2 && boot.read(1) == 0x34);
        bool rejected = false;
        try { boot.read(2); }
        catch (const std::out_of_range &) { rejected = true; }
        require(rejected && BootROM().size() == 0 && CartridgeSRAM().size() == 0);
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
