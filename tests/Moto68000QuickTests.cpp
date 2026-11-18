#include "Core/CPU/Moto68000/Moto68000.h"
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace R2MEGA::Core;

struct Bus : M68KBus
{
    std::array<std::uint16_t, 16> words{};
    std::vector<std::uint32_t> reads;
    std::uint8_t readByte(std::uint32_t) override { throw std::logic_error("byte"); }
    std::uint16_t readWord(std::uint32_t address) override
    {
        reads.push_back(address);
        return words.at(address / 2);
    }
    void writeByte(std::uint32_t, std::uint8_t) override { throw std::logic_error("write"); }
    void writeWord(std::uint32_t, std::uint16_t) override { throw std::logic_error("write"); }
};

static void require(bool value)
{
    if (!value) throw std::runtime_error("Falha no teste de ADDQ/SUBQ");
}

static void tick(Moto68000 &cpu, unsigned cycles)
{
    while (cycles--) cpu.clock();
}

struct Case
{
    std::uint16_t moveq, quick;
    std::uint32_t result;
    unsigned flags, duration;
};

int main()
{
    constexpr std::array<Case, 8> cases{{
        {0x707f, 0x5200, 0x00000080, 0x0a, 4}, // ADDQ.B: overflow positivo.
        {0x7000, 0x5300, 0x000000ff, 0x19, 4}, // SUBQ.B: empréstimo.
        {0x70ff, 0x5200, 0xffffff00, 0x15, 4}, // ADDQ.B: zero e carry.
        {0x7080, 0x5300, 0xffffff7f, 0x02, 4}, // SUBQ.B: overflow negativo.
        {0x70ff, 0x5240, 0xffff0000, 0x15, 4}, // ADDQ.W: preserva metade alta.
        {0x7000, 0x5340, 0x0000ffff, 0x19, 4}, // SUBQ.W.
        {0x70ff, 0x5080, 0x00000007, 0x11, 8}, // ADDQ.L #8 (campo zero).
        {0x7000, 0x5180, 0xfffffff8, 0x19, 8}  // SUBQ.L #8.
    }};
    for (const auto &test : cases)
    {
        Bus bus;
        bus.words[3] = 8;
        bus.words[4] = test.moveq;
        bus.words[5] = test.quick;
        bus.words[6] = bus.words[7] = 0x4e71;
        Moto68000 cpu(bus);
        cpu.reset();
        tick(cpu, 44);
        require(cpu.state().pc == 10);
        const auto reads = bus.reads.size();
        if (test.duration == 8)
        {
            tick(cpu, 4);
            require(cpu.state().pc == 10 && bus.reads.size() == reads);
        }
        tick(cpu, 3);
        require(cpu.state().pc == 10 && bus.reads.size() == reads + 1);
        tick(cpu, 1);
        require(cpu.state().pc == 12 && cpu.state().cycles == 44 + test.duration);
        require(cpu.state().data[0] == test.result && (cpu.state().sr & 0x1f) == test.flags);
        require(bus.reads.size() == reads + 1 && bus.reads.back() == 14);
    }

    // Flags calculadas pela ULA alimentam o desvio condicional.
    for (std::uint16_t branch : {std::uint16_t(0x6904), std::uint16_t(0x6804),
                                 std::uint16_t(0x6504), std::uint16_t(0x6404)})
    {
        Bus bus;
        bus.words[3] = 8;
        const bool overflowCase = branch == 0x6904 || branch == 0x6804;
        const bool taken = branch == 0x6904 || branch == 0x6504;
        bus.words[4] = overflowCase ? 0x707f : 0x7000;
        bus.words[5] = overflowCase ? 0x5200 : 0x5300;
        bus.words[6] = branch;
        bus.words[7] = 0x4e71;
        bus.words[8] = 0xffff;
        bus.words[9] = bus.words[10] = 0x4e71;
        Moto68000 cpu(bus);
        cpu.reset();
        tick(cpu, 48);
        require(cpu.state().pc == 12);
        tick(cpu, taken ? 10 : 8);
        require(cpu.state().pc == (taken ? 18u : 14u) && cpu.state().prefetch[0] == 0x4e71);
        if (taken)
            require(bus.reads[bus.reads.size() - 2] == 18 && bus.reads.back() == 20);
        else
            require(bus.reads.back() == 16);
    }
}
