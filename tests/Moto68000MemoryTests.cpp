#include "Core/CPU/Moto68000/Moto68000.h"
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace R2MEGA::Core;

struct Bus : M68KBus
{
    std::array<std::uint16_t, 16> rom{};
    std::uint16_t ram = 0;
    std::vector<std::uint32_t> reads, writes;
    std::vector<M68KPins> pins;
    unsigned readWait = 0, writeWait = 0;
    std::uint8_t readByte(std::uint32_t) override { throw std::logic_error("byte"); }
    std::uint16_t readWord(std::uint32_t address) override
    {
        reads.push_back(address);
        return address == 0xffff00 ? ram : rom.at(address / 2);
    }
    bool tryReadWord(std::uint32_t address, std::uint16_t &value) override
    {
        if (readWait && address == 0xffff00)
        {
            --readWait;
            return false;
        }
        value = readWord(address);
        return true;
    }
    void writeByte(std::uint32_t, std::uint8_t) override { throw std::logic_error("byte write"); }
    void writeWord(std::uint32_t address, std::uint16_t value) override
    {
        if (address != 0xffff00)
            throw std::logic_error("address");
        writes.push_back(address);
        ram = value;
    }
    bool tryWriteWord(std::uint32_t address, std::uint16_t value) override
    {
        if (writeWait)
        {
            --writeWait;
            return false;
        }
        writeWord(address, value);
        return true;
    }
    void observe(const M68KPins &sample) override { pins.push_back(sample); }
};

static void require(bool value)
{
    if (!value)
        throw std::runtime_error("Falha no teste de memória do 68000");
}

static void tick(Moto68000 &cpu, unsigned count)
{
    while (count--)
        cpu.clock();
}

int main()
{
    Bus bus;
    bus.rom[3] = 8;
    bus.rom[4] = 0x41f8; // LEA $FF00.W,A0
    bus.rom[5] = 0xff00;
    bus.rom[6] = 0x7034; // MOVEQ #$34,D0
    bus.rom[7] = 0x3080; // MOVE.W D0,(A0)
    bus.rom[8] = 0x3210; // MOVE.W (A0),D1
    bus.rom[9] = bus.rom[10] = bus.rom[11] = 0x4e71;
    Moto68000 cpu(bus);
    cpu.reset();
    tick(cpu, 40);
    tick(cpu, 8);
    require(cpu.state().pc == 12 && cpu.state().address[0] == 0xffffff00);
    require(bus.reads[6] == 12 && bus.reads[7] == 14);
    tick(cpu, 4);
    require(cpu.state().pc == 14 && cpu.state().data[0] == 0x34);
    bus.writeWait = 2;
    tick(cpu, 4);
    require(cpu.state().pc == 14 && bus.writes.empty());
    require(cpu.pins().write && cpu.pins().waiting && cpu.pins().functionCode == 5);
    tick(cpu, 6);
    require(cpu.state().pc == 16 && bus.ram == 0x34 && bus.writes.size() == 1);
    require(cpu.state().cycles == 62 && bus.reads.back() == 18);
    bus.readWait = 2;
    tick(cpu, 4);
    require(cpu.state().pc == 16 && bus.reads.back() == 18);
    require(cpu.pins().waiting && cpu.pins().functionCode == 5 && !cpu.pins().write);
    tick(cpu, 6);
    require(cpu.state().pc == 18 && cpu.state().data[1] == 0x34);
    require(cpu.state().cycles == 72 && bus.reads[bus.reads.size() - 2] == 0xffff00);
    require(bus.reads.back() == 20 && cpu.state().prefetch[0] == 0x4e71);
    require((cpu.state().sr & 0x0f) == 0);
}