#include "Core/CPU/Moto68000/Moto68000.h"
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace R2MEGA::Core;

struct Bus : M68KBus
{
    std::array<std::uint16_t, 32> words{};
    std::vector<std::uint32_t> reads;
    unsigned waits = 0;
    std::uint8_t readByte(std::uint32_t) override { throw std::logic_error("byte"); }
    std::uint16_t readWord(std::uint32_t address) override { return words.at(address / 2); }
    bool tryReadWord(std::uint32_t address, std::uint16_t &value) override
    {
        reads.push_back(address);
        if (waits) { --waits; return false; }
        value = readWord(address);
        return true;
    }
    void writeByte(std::uint32_t, std::uint8_t) override { throw std::logic_error("write"); }
    void writeWord(std::uint32_t, std::uint16_t) override { throw std::logic_error("write"); }
};

static void require(bool value)
{
    if (!value) throw std::runtime_error("Falha no teste das instruções do 68000");
}

static void tick(Moto68000 &cpu, unsigned count)
{
    while (count--) cpu.clock();
}

int main()
{
    Bus bus;
    bus.words[3] = 8;
    bus.words[4] = 0x74ff; // MOVEQ #-1,D2
    bus.words[5] = 0x7000; // MOVEQ #0,D0
    bus.words[6] = 0x6004; // BRA +4 -> 18
    bus.words[7] = 0x4e71;
    bus.words[8] = 0xffff;
    bus.words[9] = 0x7205; // MOVEQ #5,D1
    bus.words[10] = 0x6000; // BRA.W +6 -> 28
    bus.words[11] = 6;
    bus.words[12] = 0xffff;
    bus.words[13] = 0xffff;
    bus.words[14] = 0x4e71;
    bus.words[15] = 0x4e71;
    bus.words[16] = 0x4e71;
    Moto68000 cpu(bus);
    cpu.reset();
    tick(cpu, 40);
    require(cpu.resetComplete() && cpu.state().pc == 8);
    tick(cpu, 4);
    require(cpu.state().data[2] == 0xffffffff && (cpu.state().sr & 0xf) == 8);
    require(cpu.state().pc == 10 && cpu.state().cycles == 44);
    tick(cpu, 4);
    require(cpu.state().data[0] == 0 && (cpu.state().sr & 0xf) == 4);
    require(cpu.state().pc == 12 && cpu.state().cycles == 48);
    tick(cpu, 2);
    require(cpu.state().pc == 12 && bus.reads.back() == 14);
    bus.waits = 2;
    tick(cpu, 8);
    require(cpu.state().pc == 12 && cpu.state().cycles == 58);
    tick(cpu, 2);
    require(cpu.state().pc == 18 && cpu.state().cycles == 60);
    require(cpu.state().prefetch[0] == 0x7205 && cpu.state().prefetch[1] == 0x6000);
    require(bus.reads == std::vector<std::uint32_t>({0, 2, 4, 6, 8, 10, 12, 14, 18, 18, 18, 20}));
    tick(cpu, 4);
    require(cpu.state().data[1] == 5 && (cpu.state().sr & 0xf) == 0);
    require(cpu.state().pc == 20);
    tick(cpu, 10);
    require(cpu.state().pc == 28 && cpu.state().prefetch[0] == 0x4e71);
    require(bus.reads[bus.reads.size() - 2] == 28 && bus.reads.back() == 30);
    tick(cpu, 4);
    require(cpu.state().pc == 30 && cpu.state().instructions == 6);

    Bus loopBus;
    loopBus.words[3] = 8;
    loopBus.words[4] = 0x6000;
    loopBus.words[5] = 0xfffe; // BRA.W -2 retorna ao próprio opcode.
    Moto68000 loop(loopBus);
    loop.reset();
    tick(loop, 40);
    tick(loop, 20);
    require(loop.state().pc == 8 && loop.state().instructions == 2);
    require(loopBus.reads == std::vector<std::uint32_t>({0, 2, 4, 6, 8, 10, 8, 10, 8, 10}));
}
