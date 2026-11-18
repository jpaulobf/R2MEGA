#include "Core/CPU/Moto68000/Moto68000.h"
#include <array>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace R2MEGA::Core;

struct Bus : M68KBus
{
    std::array<std::uint16_t, 32> rom{};
    std::array<std::uint8_t, 4> ram{};
    std::vector<std::uint32_t> reads, writes;
    std::vector<M68KPins> trace;
    unsigned readWait = 0, writeWait = 0;
    unsigned byteReadWait = 0, byteWriteWait = 0;
    std::uint8_t readByte(std::uint32_t address) override
    {
        reads.push_back(address);
        return ram.at(address - 0xffff00);
    }
    bool tryReadByte(std::uint32_t address, std::uint8_t &value) override
    {
        if (byteReadWait) { --byteReadWait; return false; }
        value = readByte(address);
        return true;
    }
    std::uint16_t readWord(std::uint32_t address) override
    {
        reads.push_back(address);
        if (address < 0x100)
            return rom.at(address / 2);
        const auto offset = address - 0xffff00;
        return (std::uint16_t(ram.at(offset)) << 8) | ram.at(offset + 1);
    }
    bool tryReadWord(std::uint32_t address, std::uint16_t &value) override
    {
        if (address == 0xffff02 && readWait) { --readWait; return false; }
        value = readWord(address);
        return true;
    }
    void writeByte(std::uint32_t address, std::uint8_t value) override
    {
        writes.push_back(address);
        ram.at(address - 0xffff00) = value;
    }
    bool tryWriteByte(std::uint32_t address, std::uint8_t value) override
    {
        if (byteWriteWait) { --byteWriteWait; return false; }
        writeByte(address, value);
        return true;
    }
    void writeWord(std::uint32_t address, std::uint16_t value) override
    {
        writes.push_back(address);
        const auto offset = address - 0xffff00;
        ram.at(offset) = std::uint8_t(value >> 8);
        ram.at(offset + 1) = std::uint8_t(value);
    }
    bool tryWriteWord(std::uint32_t address, std::uint16_t value) override
    {
        if (address == 0xffff02 && writeWait) { --writeWait; return false; }
        writeWord(address, value);
        return true;
    }
    void observe(const M68KPins &pins) override { trace.push_back(pins); }
};

static void require(bool value)
{
    if (!value) throw std::runtime_error("Falha no teste de MOVE.B/L");
}

static void tick(Moto68000 &cpu, unsigned count)
{
    while (count--) cpu.clock();
}

int main()
{
    Bus bytes;
    bytes.rom[3] = 8;
    bytes.rom[4] = 0x41f8; bytes.rom[5] = 0xff01; // LEA $FF01.W,A0
    bytes.rom[6] = 0x707f; bytes.rom[7] = 0x5200; // MOVEQ #127 e ADDQ.B #1,D0
    bytes.rom[8] = 0x1080; bytes.rom[9] = 0x1210; // MOVE.B D0,(A0); MOVE.B (A0),D1
    bytes.rom[10] = bytes.rom[11] = bytes.rom[12] = 0x4e71;
    bytes.byteWriteWait = bytes.byteReadWait = 1;
    Moto68000 byteCpu(bytes);
    byteCpu.reset();
    tick(byteCpu, 64);
    require(byteCpu.state().pc == 16 && bytes.writes.size() == 1);
    tick(byteCpu, 1);
    require(byteCpu.state().pc == 18 && bytes.ram[1] == 0x80);
    require(bytes.writes == std::vector<std::uint32_t>{0xffff01});
    tick(byteCpu, 9);
    require(byteCpu.state().pc == 20 && byteCpu.state().data[1] == 0x80);
    require((byteCpu.state().sr & 0x0f) == 8 && bytes.reads[bytes.reads.size() - 2] == 0xffff01);
    bool sawOddLane = false;
    for (const auto &pins : bytes.trace)
        if (pins.address == 0xffff01 && pins.active)
        {
            require(!pins.upperDataStrobe);
            if (pins.lowerDataStrobe) sawOddLane = true;
        }
    require(sawOddLane);

    Bus even;
    even.rom = bytes.rom;
    even.rom[5] = 0xff00;
    Moto68000 evenCpu(even);
    evenCpu.reset();
    tick(evenCpu, 72);
    require(even.ram[0] == 0x80 && evenCpu.state().data[1] == 0x80);
    bool sawEvenLane = false;
    for (const auto &pins : even.trace)
        if (pins.address == 0xffff00 && pins.active)
        {
            require(!pins.lowerDataStrobe);
            if (pins.upperDataStrobe) sawEvenLane = true;
        }
    require(sawEvenLane);

    Bus longs;
    longs.rom[3] = 8;
    longs.rom[4] = 0x41f8; longs.rom[5] = 0xff00;
    longs.rom[6] = 0x70ff; longs.rom[7] = 0x5080; // MOVEQ #-1,D0; ADDQ.L #8,D0
    longs.rom[8] = 0x2080; longs.rom[9] = 0x2210; // MOVE.L D0,(A0); MOVE.L (A0),D1
    longs.rom[10] = longs.rom[11] = longs.rom[12] = 0x4e71;
    longs.writeWait = 2;
    longs.readWait = 2;
    Moto68000 longCpu(longs);
    longCpu.reset();
    tick(longCpu, 84 + 4);
    require(longCpu.state().pc == 20 && longCpu.state().data[1] == 7);
    require(longs.ram == std::array<std::uint8_t, 4>{0, 0, 0, 7});
    require(longs.writes == std::vector<std::uint32_t>({0xffff00, 0xffff02}));
    require(longs.reads[longs.reads.size() - 3] == 0xffff00 &&
            longs.reads[longs.reads.size() - 2] == 0xffff02 && longs.reads.back() == 22);
}
