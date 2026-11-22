#include "Core/CPU/Moto68000/Moto68000.h"
#include "Core/Bus/M68KBus.h"
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace R2MEGA::Core;

struct Bus : M68KBus
{
    std::array<std::uint16_t, 8> words{0x00ff, 0xff00, 0x0000, 0x0008,
                                       0x4e71, 0x4e71, 0x4e71, 0x4e71};
    std::vector<std::uint32_t> reads;
    std::vector<std::uint64_t> readCycles;
    unsigned stalls = 0;
    std::vector<M68KPins> trace;
    std::uint64_t now = 0;
    std::uint8_t readByte(std::uint32_t) override { throw std::logic_error("byte"); }
    std::uint16_t readWord(std::uint32_t address) override { return words.at(address / 2); }
    bool tryReadWord(std::uint32_t address, std::uint16_t &value) override
    {
        reads.push_back(address);
        readCycles.push_back(now);
        if (stalls)
        {
            --stalls;
            return false;
        }
        value = readWord(address);
        return true;
    }
    void observe(const M68KPins &pins) override { trace.push_back(pins); }
    void writeByte(std::uint32_t, std::uint8_t) override { throw std::logic_error("write"); }
    void writeWord(std::uint32_t, std::uint16_t) override { throw std::logic_error("write"); }
};

static void require(bool condition)
{
    if (!condition)
        throw std::runtime_error("Falha no teste do 68000");
}

static void tick(Moto68000 &cpu, Bus &bus, unsigned count)
{
    for (unsigned i = 0; i < count; ++i)
    {
        bus.now = cpu.state().cycles + 1;
        cpu.clock();
    }
}

int main()
{
    Bus bus;
    Moto68000 cpu(bus);
    tick(cpu, bus, 8);
    require(!cpu.resetComplete() && bus.reads.empty());
    cpu.reset();
    bus.trace.clear();
    require(!cpu.resetVectorsLoaded() && !cpu.resetComplete() && cpu.state().sr == 0x2700);
    tick(cpu, bus, 18);
    require(bus.reads.empty() && cpu.state().cycles == 18);
    bus.stalls = 2;
    tick(cpu, bus, 4);
    require(bus.reads == std::vector<std::uint32_t>({0, 0, 0}));
    require(bus.readCycles == std::vector<std::uint64_t>({19, 20, 21}));
    require(bus.trace[18].active && bus.trace[18].phase == 2 && bus.trace[18].waiting);
    require(bus.trace[18].addressStrobe && bus.trace[18].upperDataStrobe && bus.trace[18].lowerDataStrobe);
    require(bus.trace[18].functionCode == 6 && !bus.trace[18].dataValid);
    require(bus.trace[21].phase == 3 && bus.trace[21].dataValid && bus.trace[21].data == 0x00ff);
    tick(cpu, bus, 20);
    require(cpu.resetComplete() && cpu.resetVectorsLoaded() && cpu.state().cycles == 42);
    require(cpu.state().address[7] == 0x00ffff00 && cpu.state().pc == 8);
    require(cpu.state().prefetch == std::array<std::uint16_t, 2>{0x4e71, 0x4e71});
    require(bus.reads == std::vector<std::uint32_t>({0, 0, 0, 2, 4, 6, 8, 10}));
    require(bus.readCycles == std::vector<std::uint64_t>({19, 20, 21, 25, 29, 33, 37, 41}));
    bus.stalls = 2;
    tick(cpu, bus, 4);
    require(cpu.state().pc == 8 && cpu.state().instructions == 0);
    require(cpu.pins().waiting && cpu.pins().phase == 2 && !cpu.pins().dataValid);
    tick(cpu, bus, 2);
    require(cpu.state().pc == 10 && cpu.state().instructions == 1);
    require(bus.reads.back() == 12 && bus.readCycles.back() == 47);
    tick(cpu, bus, 4);
    require(cpu.state().pc == 12 && cpu.state().instructions == 2);
    cpu.reset();
    require(!cpu.resetComplete() && cpu.state().pc == 0 && cpu.state().instructions == 0);
}